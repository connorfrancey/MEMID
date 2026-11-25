// The standardized identity descriptor: its in-memory form, serialization, parsing, validation and compatibility checks.
//
//   auto d = memid::DescriptorBuilder{}
//                .mod_id("6f9619ff-8b86-d011-b42d-00c04fc964ff")
//                .reverse_domain("com.example.wallrun-trails")
//                .name("Wallrun Trails").publisher("Example").version("1.2.0")
//                .build();
//   if (const auto check = memid::validate(d); check.ok()) bytes = memid::serialize(d);
//
// parse() checks structure (magic, version, sizes, text encoding, extension bounds); validate() checks meaning (a mod
// id, an identifier that follows the grammar, sensible ranges). A descriptor read from a binary should pass both.
#pragma once

#include <memid/diagnostics.hpp>
#include <memid/export.hpp>
#include <memid/extensions.hpp>
#include <memid/format.hpp>
#include <memid/uuid.hpp>
#include <memid/version.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace memid
{
	struct Descriptor
	{
		std::uint16_t format_major = kFormatMajor, format_minor = kFormatMinor;
		std::uint32_t flags = 0;
		Uuid mod_id;						// Required: the mod's permanent id
		Uuid publisher_id;					// Optional (nil: not given)
		Version version;
		ReleaseChannel channel = ReleaseChannel::stable;
		Architecture architecture = Architecture::unknown;
		OperatingSystem os = OperatingSystem::unknown;
		BinaryFormat binary_format = BinaryFormat::unknown;
		Language language = Language::unknown;
		Runtime runtime = Runtime::none;
		Uuid build_id;						// Optional: one build of this version
		std::uint64_t build_timestamp = 0;	// Seconds since 1970-01-01 UTC (0: unknown)
		std::uint64_t toolchain_id = 0;		// toolchain::make(...)
		std::uint32_t game_id = game::none;
		std::uint32_t game_version_min = 0, game_version_max = 0;	// pack_game_version; 0: no limit on that side
		std::uint32_t interop_min = 0, interop_max = 0;				// 0: no limit on that side
		std::uint64_t capabilities = 0, protocol_flags = 0;
		std::string reverse_domain;			// "com.example.mod", the id in me-mod.json; at most 96 bytes
		std::string name;					// At most 64 bytes of UTF-8
		std::string publisher;				// At most 48
		std::string homepage;				// At most 64
		std::uint32_t extension_flags = 0;	// Reserved: written as given, no bits defined yet
		std::vector<Extension> extensions;	// Written after the descriptor

		friend bool operator==(const Descriptor&, const Descriptor&) = default;
	};

	struct ParseResult
	{
		std::optional<Descriptor> descriptor;
		ErrorCode error = ErrorCode::none;
		std::string message;
		std::vector<Issue> warnings;		// Readable, but something is off (reserved bytes in use, a newer minor)

		explicit operator bool() const { return descriptor.has_value(); }
	};

	// The descriptor at the start of `data`, with its extensions (which must lie inside `data`). `data` may hold more
	// after them. Never reads outside `data`.
	MEMID_API ParseResult parse(std::span<const std::uint8_t> data, const Limits& limits = {});

	// The 428 descriptor bytes followed by the extension records (extension offset and size filled in). Text longer than
	// its field is cut at a UTF-8 character boundary; validate() reports it first.
	MEMID_API std::vector<std::uint8_t> serialize(const Descriptor& d);

	// The rules a descriptor must follow to be used (see docs/MEMID_FORMAT.md, "Validation")
	MEMID_API ValidationResult validate(const Descriptor& d);

	// "com.example.mod": at least two dot-separated labels of 1-63 lowercase ASCII letters, digits or hyphens, each
	// starting and ending with a letter or digit; at most `max` bytes. me-mod.json asks for three labels; MEMID allows
	// two so a publisher's own tool can be "com.example".
	MEMID_API bool is_reverse_domain(std::string_view s, std::size_t max = field_size::reverse_domain) noexcept;

	// Well-formed UTF-8 (no overlong forms, surrogates or values past U+10FFFF)
	MEMID_API bool is_utf8(std::string_view s) noexcept;

	// ---- Compatibility ------------------------------------------------------------------------------------------------
	// Whether a binary built for `d` runs on `host` (Architecture::any matches everything)
	MEMID_API bool supports_architecture(const Descriptor& d, Architecture host) noexcept;
	MEMID_API bool supports_os(const Descriptor& d, OperatingSystem host) noexcept;
	// Whether `d` declares support for game `game_id` at packed version `version` (game::none in `d`: any game)
	MEMID_API bool supports_game(const Descriptor& d, std::uint32_t game_id, std::uint32_t version) noexcept;
	// Whether interop protocol version `version` is within `d`'s range
	MEMID_API bool supports_interop(const Descriptor& d, std::uint32_t version) noexcept;
	// Whether a reader of this library's format version can use `d` (same major)
	MEMID_API bool supports_format(const Descriptor& d) noexcept;

	// ---- Building -----------------------------------------------------------------------------------------------------
	// Fills a Descriptor. Bad text values (a UUID or version that doesn't parse) are remembered and reported by build_checked.
	class MEMID_API DescriptorBuilder
	{
	public:
		DescriptorBuilder();	// Architecture, OS, binary format and toolchain of the calling program
		DescriptorBuilder& mod_id(std::string_view uuid);
		DescriptorBuilder& mod_id(const Uuid& id);
		DescriptorBuilder& publisher_id(std::string_view uuid);
		DescriptorBuilder& reverse_domain(std::string_view id);
		DescriptorBuilder& name(std::string_view text);
		DescriptorBuilder& publisher(std::string_view text);
		DescriptorBuilder& homepage(std::string_view url);
		DescriptorBuilder& version(std::string_view semver);
		DescriptorBuilder& version(const Version& v);
		DescriptorBuilder& channel(ReleaseChannel c);
		DescriptorBuilder& architecture(Architecture a);
		DescriptorBuilder& os(OperatingSystem o);
		DescriptorBuilder& binary_format(BinaryFormat f);
		DescriptorBuilder& language(Language l);
		DescriptorBuilder& runtime(Runtime r);
		DescriptorBuilder& build_id(std::string_view uuid);
		DescriptorBuilder& build_timestamp(std::uint64_t seconds);
		DescriptorBuilder& game(std::uint32_t id, std::uint32_t min_version = 0, std::uint32_t max_version = 0);
		DescriptorBuilder& interop(std::uint32_t min_version, std::uint32_t max_version);
		DescriptorBuilder& capabilities(std::uint64_t bits);
		DescriptorBuilder& protocol_flags(std::uint64_t bits);
		DescriptorBuilder& flags(std::uint32_t bits);
		DescriptorBuilder& extension(Extension record);

		Descriptor build() const { return d_; }
		// The descriptor, and the builder's own problems (values that didn't parse) followed by validate()'s
		ValidationResult build_checked(Descriptor& out) const;

	private:
		Descriptor d_;
		std::vector<std::string> bad_;
	};
}
