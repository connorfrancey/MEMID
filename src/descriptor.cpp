// The descriptor: parse (structure), serialize, validate (meaning), compatibility checks and the builder.

#include <memid/descriptor.hpp>
#include <memid/detail/codec.hpp>

#include <algorithm>

namespace memid
{
	namespace
	{
		using detail::get16;
		using detail::get32;
		using detail::get64;

		Uuid ReadUuid(const std::uint8_t* p)
		{
			Uuid u;
			std::copy(p, p + 16, u.bytes.begin());
			return u;
		}

		// A NUL-padded text field: the text ends at the first NUL (or fills the field); every byte after it must be NUL
		bool ReadText(const std::uint8_t* p, std::size_t size, std::string& out)
		{
			std::size_t len = 0;
			while (len < size && p[len] != 0) ++len;
			for (std::size_t i = len; i < size; ++i)
				if (p[i] != 0) return false;
			out.assign(reinterpret_cast<const char*>(p), len);
			return true;
		}

		// `s` cut to at most `max` bytes without splitting a UTF-8 character
		std::string_view CutUtf8(std::string_view s, std::size_t max)
		{
			if (s.size() <= max) return s;
			std::size_t n = max;
			while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
			return s.substr(0, n);
		}

		bool HasControl(std::string_view s)
		{
			return std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
		}

		ParseResult Fail(ErrorCode code, std::string message)
		{
			ParseResult r;
			r.error = code;
			r.message = std::move(message);
			return r;
		}
	}

	std::string_view to_string(ErrorCode code) noexcept
	{
		switch (code) {
		case ErrorCode::none: return "none";
		case ErrorCode::truncated: return "truncated";
		case ErrorCode::bad_magic: return "bad magic";
		case ErrorCode::unsupported_version: return "unsupported version";
		case ErrorCode::malformed: return "malformed";
		case ErrorCode::limit_exceeded: return "limit exceeded";
		case ErrorCode::invalid_field: return "invalid field";
		case ErrorCode::unknown_critical: return "unknown critical extension";
		case ErrorCode::not_found: return "not found";
		case ErrorCode::io: return "I/O error";
		case ErrorCode::unsupported_binary: return "unsupported binary format";
		}
		return "unknown";
	}

	bool is_utf8(std::string_view s) noexcept
	{
		const auto* p = reinterpret_cast<const unsigned char*>(s.data());
		const std::size_t n = s.size();
		for (std::size_t i = 0; i < n;) {
			const unsigned char c = p[i];
			std::size_t len;
			std::uint32_t cp;
			if (c < 0x80) {
				++i;
				continue;
			} else if ((c & 0xE0) == 0xC0) {
				len = 2, cp = c & 0x1F;
			} else if ((c & 0xF0) == 0xE0) {
				len = 3, cp = c & 0x0F;
			} else if ((c & 0xF8) == 0xF0) {
				len = 4, cp = c & 0x07;
			} else
				return false;
			if (n - i < len) return false;
			for (std::size_t k = 1; k < len; ++k) {
				if ((p[i + k] & 0xC0) != 0x80) return false;
				cp = cp << 6 | (p[i + k] & 0x3F);
			}
			if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;	// Overlong
			if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
			i += len;
		}
		return true;
	}

	bool is_reverse_domain(std::string_view s, std::size_t max) noexcept
	{
		if (s.empty() || s.size() > max) return false;
		std::size_t labels = 0, start = 0;
		while (start <= s.size()) {
			const std::size_t dot = std::min(s.find('.', start), s.size());
			const std::string_view label = s.substr(start, dot - start);
			if (label.empty() || label.size() > 63) return false;
			for (char c : label)
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
			if (label.front() == '-' || label.back() == '-') return false;
			++labels;
			start = dot + 1;
		}
		return labels >= 2;
	}

	ParseResult parse(std::span<const std::uint8_t> data, const Limits& limits)
	{
		if (data.size() < offsets::flags) return Fail(ErrorCode::truncated, "fewer bytes than a descriptor header");
		if (!std::equal(kMagic.begin(), kMagic.end(), data.begin())) return Fail(ErrorCode::bad_magic, "not a MEMID descriptor");
		const std::uint8_t* p = data.data();
		const std::uint16_t major = get16(p + offsets::format_major), minor = get16(p + offsets::format_minor);
		if (major != kFormatMajor)
			return Fail(ErrorCode::unsupported_version, "format " + std::to_string(major) + "." + std::to_string(minor) +
															 " (this reader implements " + std::to_string(kFormatMajor) + ".x)");
		const std::uint32_t size = get32(p + offsets::descriptor_size);
		if (size < kDescriptorSize)
			return Fail(ErrorCode::malformed, "descriptor size " + std::to_string(size) + " is below the " +
												  std::to_string(kDescriptorSize) + " bytes of format 1.1");
		if (size > limits.max_identity_section) return Fail(ErrorCode::limit_exceeded, "descriptor size " + std::to_string(size));
		if (size > data.size())
			return Fail(ErrorCode::truncated, "descriptor of " + std::to_string(size) + " bytes in " + std::to_string(data.size()));

		ParseResult r;
		Descriptor d;
		d.format_major = major;
		d.format_minor = minor;
		if (minor > kFormatMinor || size > kDescriptorSize)
			r.warnings.push_back({Issue::Severity::warning, ErrorCode::none,
								  "format " + std::to_string(major) + "." + std::to_string(minor) + ", " + std::to_string(size) +
									  " bytes: fields after the 1.1 ones are not read"});
		d.flags = get32(p + offsets::flags);
		d.mod_id = ReadUuid(p + offsets::mod_id);
		d.publisher_id = ReadUuid(p + offsets::publisher_id);
		d.version = {get16(p + offsets::version_major), get16(p + offsets::version_minor), get16(p + offsets::version_patch)};
		d.channel = static_cast<ReleaseChannel>(get16(p + offsets::release_channel));
		d.architecture = static_cast<Architecture>(get16(p + offsets::architecture));
		d.os = static_cast<OperatingSystem>(get16(p + offsets::operating_system));
		d.binary_format = static_cast<BinaryFormat>(p[offsets::binary_format]);
		d.language = static_cast<Language>(p[offsets::language]);
		d.runtime = static_cast<Runtime>(get16(p + offsets::runtime));
		d.build_id = ReadUuid(p + offsets::build_id);
		d.build_timestamp = get64(p + offsets::build_timestamp);
		d.toolchain_id = get64(p + offsets::toolchain_id);
		d.game_id = get32(p + offsets::game_id);
		d.game_version_min = get32(p + offsets::game_version_min);
		d.game_version_max = get32(p + offsets::game_version_max);
		d.interop_min = get32(p + offsets::interop_min);
		d.interop_max = get32(p + offsets::interop_max);
		d.capabilities = get64(p + offsets::capabilities);
		d.protocol_flags = get64(p + offsets::protocol_flags);
		for (std::size_t i = 0; i < field_size::reserved; ++i)
			if (p[offsets::reserved + i]) {
				r.warnings.push_back({Issue::Severity::warning, ErrorCode::none, "reserved bytes 120-127 are not zero"});
				break;
			}

		struct Text { std::size_t at, size; std::string* out; const char* what; };
		const Text texts[] = {{offsets::reverse_domain, field_size::reverse_domain, &d.reverse_domain, "reverse-domain id"},
							  {offsets::name, field_size::name, &d.name, "name"},
							  {offsets::publisher, field_size::publisher, &d.publisher, "publisher"},
							  {offsets::homepage, field_size::homepage, &d.homepage, "homepage"}};
		for (const Text& t : texts) {
			if (!ReadText(p + t.at, t.size, *t.out)) return Fail(ErrorCode::malformed, std::string(t.what) + ": bytes after its terminating NUL");
			if (!is_utf8(*t.out)) return Fail(ErrorCode::invalid_field, std::string(t.what) + ": not UTF-8");
		}

		// Extensions: inside `data`, after the descriptor
		const std::uint32_t extOffset = get32(p + offsets::extension_offset), extSize = get32(p + offsets::extension_size);
		d.extension_flags = get32(p + offsets::extension_flags);
		if (extSize > 0) {
			const std::uint64_t end = std::uint64_t{extOffset} + extSize;	// No overflow in 64 bits
			if (extOffset < size) return Fail(ErrorCode::malformed, "extensions overlap the descriptor");
			if (end > data.size()) return Fail(ErrorCode::truncated, "extensions run past the end of the section");
			if (end > limits.max_identity_section) return Fail(ErrorCode::limit_exceeded, "extensions end at " + std::to_string(end));
			ExtensionParse ext = parse_extensions(data.subspan(extOffset, extSize), limits);
			if (!ext) return Fail(ext.error, "extensions: " + ext.message);
			for (const Extension& e : ext.records)
				if (e.critical() && !is_known_extension(e.type))
					return Fail(ErrorCode::unknown_critical, "critical extension type " + std::to_string(e.type) + " is unknown to this reader");
			d.extensions = std::move(ext.records);
		}
		r.descriptor = std::move(d);
		return r;
	}

	std::vector<std::uint8_t> serialize(const Descriptor& d)
	{
		detail::Fields f;
		f.format_major = d.format_major;
		f.format_minor = d.format_minor;
		f.flags = d.flags;
		f.mod_id = d.mod_id;
		f.publisher_id = d.publisher_id;
		f.version = d.version;
		f.release_channel = static_cast<std::uint16_t>(d.channel);
		f.architecture = static_cast<std::uint16_t>(d.architecture);
		f.operating_system = static_cast<std::uint16_t>(d.os);
		f.binary_format = static_cast<std::uint8_t>(d.binary_format);
		f.language = static_cast<std::uint8_t>(d.language);
		f.runtime = static_cast<std::uint16_t>(d.runtime);
		f.build_id = d.build_id;
		f.build_timestamp = d.build_timestamp;
		f.toolchain_id = d.toolchain_id;
		f.game_id = d.game_id;
		f.game_version_min = d.game_version_min;
		f.game_version_max = d.game_version_max;
		f.interop_min = d.interop_min;
		f.interop_max = d.interop_max;
		f.capabilities = d.capabilities;
		f.protocol_flags = d.protocol_flags;
		f.reverse_domain = CutUtf8(d.reverse_domain, field_size::reverse_domain);
		f.name = CutUtf8(d.name, field_size::name);
		f.publisher = CutUtf8(d.publisher, field_size::publisher);
		f.homepage = CutUtf8(d.homepage, field_size::homepage);
		f.extension_flags = d.extension_flags;
		std::vector<std::uint8_t> ext = serialize_extensions(d.extensions);
		if (!ext.empty()) {
			f.extension_offset = kDescriptorSize;
			f.extension_size = static_cast<std::uint32_t>(ext.size());
		}
		const detail::Bytes bytes = detail::write(f);
		std::vector<std::uint8_t> out(bytes.begin(), bytes.end());
		out.insert(out.end(), ext.begin(), ext.end());
		return out;
	}

	ValidationResult validate(const Descriptor& d)
	{
		ValidationResult v;
		const auto error = [&v](ErrorCode code, std::string m) { v.issues.push_back({Issue::Severity::error, code, std::move(m)}); };
		const auto warn = [&v](std::string m) { v.issues.push_back({Issue::Severity::warning, ErrorCode::none, std::move(m)}); };

		if (d.format_major != kFormatMajor) error(ErrorCode::unsupported_version, "format major " + std::to_string(d.format_major));
		if (d.mod_id.nil()) error(ErrorCode::invalid_field, "mod id: missing (a nil UUID)");
		if (!is_reverse_domain(d.reverse_domain))
			error(ErrorCode::invalid_field, "reverse-domain id \"" + d.reverse_domain +
												"\": needs two or more dot-separated labels of a-z, 0-9 and '-' (at most 96 bytes)");
		struct Text { const std::string* s; std::size_t max; const char* what; bool required; };
		for (const Text& t : {Text{&d.name, field_size::name, "name", true}, Text{&d.publisher, field_size::publisher, "publisher", false},
							  Text{&d.homepage, field_size::homepage, "homepage", false}}) {
			if (t.required && t.s->empty()) error(ErrorCode::invalid_field, std::string(t.what) + ": missing");
			if (t.s->size() > t.max)
				error(ErrorCode::invalid_field, std::string(t.what) + ": " + std::to_string(t.s->size()) + " bytes (at most " +
													std::to_string(t.max) + ")");
			if (!is_utf8(*t.s)) error(ErrorCode::invalid_field, std::string(t.what) + ": not UTF-8");
			if (HasControl(*t.s)) error(ErrorCode::invalid_field, std::string(t.what) + ": control characters");
		}
		if (!d.name.empty() && (d.name.front() == ' ' || d.name.back() == ' '))
			error(ErrorCode::invalid_field, "name: leading or trailing spaces");
		if (!d.homepage.empty() && d.homepage.rfind("https://", 0) != 0 && d.homepage.rfind("http://", 0) != 0)
			warn("homepage doesn't start with https:// or http://");
		if (d.game_version_min && d.game_version_max && d.game_version_min > d.game_version_max)
			error(ErrorCode::invalid_field, "game version range: minimum above maximum");
		if (d.interop_min && d.interop_max && d.interop_min > d.interop_max)
			error(ErrorCode::invalid_field, "interop range: minimum above maximum");
		if (d.game_id == game::none && (d.game_version_min || d.game_version_max))
			warn("a game version range without a game id is ignored");
		if (static_cast<std::uint16_t>(d.channel) > static_cast<std::uint16_t>(ReleaseChannel::nightly)) warn("release channel unknown to this reader");
		for (const Extension& e : d.extensions)
			if (e.critical() && !is_known_extension(e.type))
				error(ErrorCode::unknown_critical, "critical extension type " + std::to_string(e.type) + " is unknown to this reader");
		const std::size_t extBytes = serialize_extensions(d.extensions).size();
		if (kDescriptorSize + extBytes > Limits{}.max_identity_section)
			error(ErrorCode::limit_exceeded, "extensions make the section larger than readers accept");
		return v;
	}

	bool supports_architecture(const Descriptor& d, Architecture host) noexcept
	{
		return d.architecture == Architecture::any || d.architecture == Architecture::unknown || d.architecture == host;
	}
	bool supports_os(const Descriptor& d, OperatingSystem host) noexcept
	{
		return d.os == OperatingSystem::any || d.os == OperatingSystem::unknown || d.os == host;
	}
	bool supports_game(const Descriptor& d, std::uint32_t game_id, std::uint32_t version) noexcept
	{
		if (d.game_id == game::none) return true;
		if (d.game_id != game_id) return false;
		return (d.game_version_min == 0 || version >= d.game_version_min) && (d.game_version_max == 0 || version <= d.game_version_max);
	}
	bool supports_interop(const Descriptor& d, std::uint32_t version) noexcept
	{
		return (d.interop_min == 0 || version >= d.interop_min) && (d.interop_max == 0 || version <= d.interop_max);
	}
	bool supports_format(const Descriptor& d) noexcept { return d.format_major == kFormatMajor; }

	// ---- The builder ----------------------------------------------------------------------------------------------------
	DescriptorBuilder::DescriptorBuilder()
	{
		d_.architecture = detail::host_architecture();
		d_.os = detail::host_os();
		d_.binary_format = detail::host_binary_format();
		d_.toolchain_id = detail::host_toolchain();
	}
	DescriptorBuilder& DescriptorBuilder::mod_id(std::string_view uuid)
	{
		if (const auto u = Uuid::parse(uuid)) d_.mod_id = *u;
		else bad_.push_back("mod id \"" + std::string(uuid) + "\" is not a UUID");
		return *this;
	}
	DescriptorBuilder& DescriptorBuilder::mod_id(const Uuid& id) { d_.mod_id = id; return *this; }
	DescriptorBuilder& DescriptorBuilder::publisher_id(std::string_view uuid)
	{
		if (const auto u = Uuid::parse(uuid)) d_.publisher_id = *u;
		else bad_.push_back("publisher id \"" + std::string(uuid) + "\" is not a UUID");
		return *this;
	}
	DescriptorBuilder& DescriptorBuilder::reverse_domain(std::string_view id) { d_.reverse_domain = id; return *this; }
	DescriptorBuilder& DescriptorBuilder::name(std::string_view text) { d_.name = text; return *this; }
	DescriptorBuilder& DescriptorBuilder::publisher(std::string_view text) { d_.publisher = text; return *this; }
	DescriptorBuilder& DescriptorBuilder::homepage(std::string_view url) { d_.homepage = url; return *this; }
	DescriptorBuilder& DescriptorBuilder::version(std::string_view semver)
	{
		if (const auto v = Version::parse(semver)) d_.version = *v;
		else bad_.push_back("version \"" + std::string(semver) + "\" is not MAJOR.MINOR.PATCH");
		return *this;
	}
	DescriptorBuilder& DescriptorBuilder::version(const Version& v) { d_.version = v; return *this; }
	DescriptorBuilder& DescriptorBuilder::channel(ReleaseChannel c) { d_.channel = c; return *this; }
	DescriptorBuilder& DescriptorBuilder::architecture(Architecture a) { d_.architecture = a; return *this; }
	DescriptorBuilder& DescriptorBuilder::os(OperatingSystem o) { d_.os = o; return *this; }
	DescriptorBuilder& DescriptorBuilder::binary_format(BinaryFormat f) { d_.binary_format = f; return *this; }
	DescriptorBuilder& DescriptorBuilder::language(Language l) { d_.language = l; return *this; }
	DescriptorBuilder& DescriptorBuilder::runtime(Runtime r) { d_.runtime = r; return *this; }
	DescriptorBuilder& DescriptorBuilder::build_id(std::string_view uuid)
	{
		if (const auto u = Uuid::parse(uuid)) d_.build_id = *u;
		else bad_.push_back("build id \"" + std::string(uuid) + "\" is not a UUID");
		return *this;
	}
	DescriptorBuilder& DescriptorBuilder::build_timestamp(std::uint64_t seconds) { d_.build_timestamp = seconds; return *this; }
	DescriptorBuilder& DescriptorBuilder::game(std::uint32_t id, std::uint32_t min_version, std::uint32_t max_version)
	{
		d_.game_id = id;
		d_.game_version_min = min_version;
		d_.game_version_max = max_version;
		return *this;
	}
	DescriptorBuilder& DescriptorBuilder::interop(std::uint32_t min_version, std::uint32_t max_version)
	{
		d_.interop_min = min_version;
		d_.interop_max = max_version;
		return *this;
	}
	DescriptorBuilder& DescriptorBuilder::capabilities(std::uint64_t bits) { d_.capabilities = bits; return *this; }
	DescriptorBuilder& DescriptorBuilder::protocol_flags(std::uint64_t bits) { d_.protocol_flags = bits; return *this; }
	DescriptorBuilder& DescriptorBuilder::flags(std::uint32_t bits) { d_.flags = bits; return *this; }
	DescriptorBuilder& DescriptorBuilder::extension(Extension record) { d_.extensions.push_back(std::move(record)); return *this; }

	ValidationResult DescriptorBuilder::build_checked(Descriptor& out) const
	{
		out = d_;
		ValidationResult v;
		for (const std::string& b : bad_) v.issues.push_back({Issue::Severity::error, ErrorCode::invalid_field, b});
		ValidationResult rest = validate(d_);
		v.issues.insert(v.issues.end(), rest.issues.begin(), rest.issues.end());
		return v;
	}
}
