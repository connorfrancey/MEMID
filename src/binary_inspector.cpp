// Reading a binary's section table and its MEMID sections without loading it. Every read goes through Source::read,
// which refuses anything outside the file.

#include <memid/binary_inspector.hpp>
#include <memid/detail/codec.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>

namespace memid
{
	namespace
	{
		using detail::get16;
		using detail::get32;
		using detail::get64;

		// The file's bytes, from memory or disk
		class Source
		{
		public:
			virtual ~Source() = default;
			virtual std::uint64_t size() const = 0;
			// `n` bytes at `offset` into `out`; false (and nothing read) if any of them is outside the file
			virtual bool read(std::uint64_t offset, void* out, std::size_t n) const = 0;

			bool fits(std::uint64_t offset, std::uint64_t n) const { return offset <= size() && n <= size() - offset; }
			std::vector<std::uint8_t> bytes(std::uint64_t offset, std::size_t n) const
			{
				std::vector<std::uint8_t> v(n);
				if (!read(offset, v.data(), n)) v.clear();
				return v;
			}
		};

		class MemorySource final : public Source
		{
		public:
			explicit MemorySource(std::span<const std::uint8_t> data) : data_(data) {}
			std::uint64_t size() const override { return data_.size(); }
			bool read(std::uint64_t offset, void* out, std::size_t n) const override
			{
				if (!fits(offset, n)) return false;
				if (n) std::memcpy(out, data_.data() + offset, n);
				return true;
			}

		private:
			std::span<const std::uint8_t> data_;
		};

		class FileSource final : public Source
		{
		public:
			explicit FileSource(const std::filesystem::path& path) : in_(path, std::ios::binary)
			{
				if (!in_) return;
				in_.seekg(0, std::ios::end);
				const auto end = in_.tellg();
				if (end >= 0) size_ = static_cast<std::uint64_t>(end), ok_ = true;
			}
			bool ok() const { return ok_; }
			std::uint64_t size() const override { return size_; }
			bool read(std::uint64_t offset, void* out, std::size_t n) const override
			{
				if (!ok_ || !fits(offset, n)) return false;
				if (n == 0) return true;
				in_.clear();
				in_.seekg(static_cast<std::streamoff>(offset));
				in_.read(static_cast<char*>(out), static_cast<std::streamsize>(n));
				return static_cast<std::size_t>(in_.gcount()) == n;
			}

		private:
			mutable std::ifstream in_;
			std::uint64_t size_ = 0;
			bool ok_ = false;
		};

		void FileError(Inspection& i, ErrorCode code, std::string message)
		{
			i.file_error = code;
			i.file_message = std::move(message);
		}

		Architecture PeMachine(std::uint16_t m)
		{
			switch (m) {
			case 0x014C: return Architecture::x86;
			case 0x8664: return Architecture::x86_64;
			case 0xAA64: return Architecture::arm64;
			case 0x01C4: return Architecture::arm;
			default: return Architecture::unknown;
			}
		}
		Architecture ElfMachine(std::uint16_t m)
		{
			switch (m) {
			case 3: return Architecture::x86;
			case 62: return Architecture::x86_64;
			case 40: return Architecture::arm;
			case 183: return Architecture::arm64;
			default: return Architecture::unknown;
			}
		}

		// What the resource lookup needs of a PE beyond the section table: where each section is mapped, and the
		// resource directory's place
		struct PeImage
		{
			struct Mapped { std::uint32_t va, raw_offset, raw_size; };
			std::vector<Mapped> sections;
			std::uint32_t resource_rva = 0, resource_size = 0;

			// File offset of `n` bytes at `rva`, all inside one section's file data
			std::optional<std::uint64_t> offset(std::uint32_t rva, std::uint32_t n) const
			{
				for (const Mapped& s : sections)
					if (rva >= s.va && rva - s.va < s.raw_size && n <= s.raw_size - (rva - s.va))
						return std::uint64_t{s.raw_offset} + (rva - s.va);
				return std::nullopt;
			}
		};

		void ReadPe(const Source& src, Inspection& i, PeImage& image)
		{
			std::uint8_t dos[0x40];
			if (!src.read(0, dos, sizeof dos)) return FileError(i, ErrorCode::malformed, "PE: shorter than a DOS header");
			const std::uint32_t nt = get32(dos + 0x3C);
			std::uint8_t coff[24];	// "PE\0\0" and the COFF file header
			if (!src.read(nt, coff, sizeof coff) || std::memcmp(coff, "PE\0\0", 4) != 0)
				return FileError(i, ErrorCode::malformed, "PE: no NT headers where the DOS header points");
			i.format = BinaryFormat::pe;
			i.architecture = PeMachine(get16(coff + 4));
			const std::uint16_t count = get16(coff + 6), optional = get16(coff + 20);
			const std::uint64_t table = std::uint64_t{nt} + 24 + optional;
			// The resource directory: data directory 2 of the optional header (PE32 or PE32+), when it has one
			std::uint8_t opt[136]{};	// Up to PE32+'s data directory 2
			if (optional >= 2 && src.read(std::uint64_t{nt} + 24, opt, std::min<std::size_t>(optional, sizeof opt))) {
				const std::uint16_t kind = get16(opt);
				const std::size_t dirs = kind == 0x20B ? 112 : kind == 0x10B ? 96 : 0;	// PE32+ : PE32
				if (dirs && optional >= dirs && get32(opt + dirs - 4) > 2 && optional >= dirs + 24) {
					image.resource_rva = get32(opt + dirs + 16);
					image.resource_size = get32(opt + dirs + 20);
				}
			}
			if (!src.fits(table, std::uint64_t{count} * 40)) return FileError(i, ErrorCode::malformed, "PE: the section table runs past the end of the file");
			for (std::uint16_t s = 0; s < count; ++s) {
				std::uint8_t h[40];
				src.read(table + std::uint64_t{s} * 40, h, sizeof h);
				SectionInfo info;
				info.name.assign(reinterpret_cast<const char*>(h), strnlen(reinterpret_cast<const char*>(h), 8));
				const std::uint32_t virtualSize = get32(h + 8), rawSize = get32(h + 16), rawOffset = get32(h + 20);
				info.file_offset = rawOffset;
				info.size = virtualSize ? std::min(virtualSize, rawSize) : rawSize;	// Raw data is padded to the file alignment
				i.sections.push_back(std::move(info));
				image.sections.push_back({get32(h + 12), rawOffset, rawSize});
			}
		}

		// The data of the RT_RCDATA resource named `name` (ASCII, any case; its first language): file offset and size.
		// Every directory and entry read is checked against the resource directory's size and the file.
		std::optional<SectionInfo> FindPeResource(const Source& src, const PeImage& image, std::string_view name)
		{
			constexpr std::uint32_t kRcData = 10, kSubdirectory = 0x80000000u, kMaxEntries = 4096;
			const std::uint32_t size = image.resource_size;
			const std::optional<std::uint64_t> base = image.offset(image.resource_rva, 16);
			if (!base || size < 16) return std::nullopt;
			const auto read = [&](std::uint32_t at, void* out, std::size_t n) {
				return at <= size && n <= size - at && src.read(*base + at, out, n);
			};
			// The entries of the directory at `at`: {name or id, data or subdirectory}
			const auto entries = [&](std::uint32_t at) {
				std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
				std::uint8_t dir[16];
				if (!read(at, dir, sizeof dir)) return out;
				const std::uint32_t n = std::min<std::uint32_t>(kMaxEntries, std::uint32_t{get16(dir + 12)} + get16(dir + 14));
				for (std::uint32_t e = 0; e < n; ++e) {
					std::uint8_t entry[8];
					if (!read(at + 16 + e * 8, entry, sizeof entry)) break;
					out.emplace_back(get32(entry), get32(entry + 4));
				}
				return out;
			};
			const auto named = [&](std::uint32_t id) {	// A named entry's name is a counted UTF-16 string
				if (!(id & kSubdirectory)) return false;
				const std::uint32_t at = id & ~kSubdirectory;
				std::uint8_t length[2];
				if (!read(at, length, 2) || get16(length) != name.size()) return false;
				std::vector<std::uint8_t> text(name.size() * 2);
				if (!read(at + 2, text.data(), text.size())) return false;
				for (std::size_t c = 0; c < name.size(); ++c) {
					const std::uint16_t ch = get16(&text[c * 2]);
					if (ch > 0x7F || std::toupper(ch) != std::toupper(static_cast<unsigned char>(name[c]))) return false;
				}
				return true;
			};
			for (const auto& [type, typeDir] : entries(0)) {
				if (type != kRcData || !(typeDir & kSubdirectory)) continue;
				for (const auto& [id, nameDir] : entries(typeDir & ~kSubdirectory)) {
					if (!named(id) || !(nameDir & kSubdirectory)) continue;
					const auto languages = entries(nameDir & ~kSubdirectory);
					if (languages.empty() || (languages.front().second & kSubdirectory)) return std::nullopt;
					std::uint8_t data[16];	// IMAGE_RESOURCE_DATA_ENTRY: RVA, size, code page, reserved
					if (!read(languages.front().second, data, sizeof data)) return std::nullopt;
					const std::optional<std::uint64_t> at = image.offset(get32(data), get32(data + 4));
					if (!at) return std::nullopt;
					return SectionInfo{std::string(name), *at, get32(data + 4)};
				}
			}
			return std::nullopt;
		}

		void ReadElf(const Source& src, Inspection& i)
		{
			std::uint8_t h[64];
			const bool is64 = [&] {
				std::uint8_t ident[6];
				return src.read(0, ident, 6) && ident[4] == 2;
			}();
			if (!src.read(0, h, is64 ? 64 : 52)) return FileError(i, ErrorCode::malformed, "ELF: shorter than its header");
			if (h[4] != 1 && h[4] != 2) return FileError(i, ErrorCode::malformed, "ELF: unknown class");
			i.format = BinaryFormat::elf;
			if (h[5] != 1) return FileError(i, ErrorCode::unsupported_binary, "ELF: big-endian files are not supported");
			i.architecture = ElfMachine(get16(h + 18));
			const std::uint64_t shoff = is64 ? get64(h + 40) : get32(h + 32);
			const std::uint16_t entSize = get16(h + (is64 ? 58 : 46)), count = get16(h + (is64 ? 60 : 48)),
								strIndex = get16(h + (is64 ? 62 : 50));
			if (count == 0 || shoff == 0) return;	// No section table
			const std::size_t need = is64 ? 64 : 40;
			if (entSize < need || !src.fits(shoff, std::uint64_t{count} * entSize))
				return FileError(i, ErrorCode::malformed, "ELF: the section table runs past the end of the file");
			struct Raw { std::uint32_t name, type; std::uint64_t offset, size; };
			std::vector<Raw> raw;
			for (std::uint16_t s = 0; s < count; ++s) {
				std::uint8_t sh[64];
				src.read(shoff + std::uint64_t{s} * entSize, sh, need);
				raw.push_back(is64 ? Raw{get32(sh), get32(sh + 4), get64(sh + 24), get64(sh + 32)}
								   : Raw{get32(sh), get32(sh + 4), get32(sh + 16), get32(sh + 20)});
			}
			if (strIndex >= raw.size()) return FileError(i, ErrorCode::malformed, "ELF: no section name table");
			const Raw& strtab = raw[strIndex];
			for (const Raw& r : raw) {
				SectionInfo info;
				// The name: NUL-terminated inside the name table (at most 64 bytes read)
				if (r.name < strtab.size) {
					char name[65]{};
					const std::uint64_t at = strtab.offset + r.name;
					const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(64, strtab.size - r.name));
					if (src.read(at, name, n)) info.name.assign(name, strnlen(name, n));
				}
				info.file_offset = r.offset;
				info.size = r.type == 8 ? 0 : r.size;	// SHT_NOBITS: nothing in the file
				i.sections.push_back(std::move(info));
			}
		}

		void Inspect(const Source& src, Inspection& i, const Limits& limits)
		{
			std::uint8_t magic[4]{};
			if (!src.read(0, magic, 4)) return FileError(i, ErrorCode::unsupported_binary, "too short to be a binary");
			const std::uint32_t m = get32(magic);
			PeImage pe;
			if (magic[0] == 'M' && magic[1] == 'Z')
				ReadPe(src, i, pe);
			else if (std::memcmp(magic, "\x7F" "ELF", 4) == 0)
				ReadElf(src, i);
			else if (m == 0xFEEDFACE || m == 0xCEFAEDFE || m == 0xFEEDFACF || m == 0xCFFAEDFE || m == 0xBEBAFECA) {
				i.format = BinaryFormat::macho;
				return FileError(i, ErrorCode::unsupported_binary, "Mach-O files are not supported yet");
			} else
				return FileError(i, ErrorCode::unsupported_binary, "not a PE or ELF file");
			if (i.file_error != ErrorCode::none) return;

			// The section of that name; else, in a PE, the RT_RCDATA resource `resource`
			const auto locate = [&](std::string_view name, std::string_view resource, Location& where) -> std::optional<SectionInfo> {
				for (const SectionInfo& s : i.sections)
					if (s.name == name) {
						where = Location::section;
						return s;
					}
				if (i.format == BinaryFormat::pe)
					if (auto r = FindPeResource(src, pe, resource)) {
						where = Location::resource;
						r->name = "resource " + std::string(resource);
						return r;
					}
				return std::nullopt;
			};

			if (const std::optional<SectionInfo> s = locate(kIdentitySection, "MEMID", i.identity_location)) {
				i.has_identity_section = true;
				if (!src.fits(s->file_offset, s->size)) {
					i.identity_error = ErrorCode::malformed;
					i.identity_message = s->name + " lies outside the file";
				} else if (s->size > limits.max_identity_section) {
					i.identity_error = ErrorCode::limit_exceeded;
					i.identity_message = s->name + " is " + std::to_string(s->size) + " bytes";
				} else {
					const std::vector<std::uint8_t> bytes = src.bytes(s->file_offset, static_cast<std::size_t>(s->size));
					bool second = false;
					const std::size_t at = find_descriptor(bytes, &second);
					if (at == std::numeric_limits<std::size_t>::max()) {
						i.identity_error = ErrorCode::not_found;
						i.identity_message = s->name + " holds no descriptor";
					} else if (second) {
						i.identity_error = ErrorCode::malformed;
						i.identity_message = s->name + " holds more than one descriptor (one identity per binary)";
					} else {
						ParseResult p = parse(std::span(bytes).subspan(at), limits);
						i.identity_warnings = std::move(p.warnings);
						if (p) {
							i.identity_error = ErrorCode::none;
							i.validation = validate(*p.descriptor);
							i.descriptor = std::move(p.descriptor);
							const std::size_t used = static_cast<std::size_t>(std::max<std::uint64_t>(
								detail::get32(bytes.data() + at + offsets::descriptor_size),
								std::uint64_t{detail::get32(bytes.data() + at + offsets::extension_offset)} +
									detail::get32(bytes.data() + at + offsets::extension_size)));
							i.identity_bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
													bytes.begin() + static_cast<std::ptrdiff_t>(at + std::min(used, bytes.size() - at)));
						} else {
							i.identity_error = p.error;
							i.identity_message = std::move(p.message);
						}
					}
				}
			} else
				i.identity_message = "no .memid section";

			if (const std::optional<SectionInfo> s = locate(kCustomSection, "MEMIDX", i.custom_location)) {
				i.has_custom_section = true;
				if (!src.fits(s->file_offset, s->size)) {
					i.custom.issues.push_back({Issue::Severity::error, ErrorCode::malformed, s->name + " lies outside the file"});
				} else {
					const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(s->size, limits.max_custom_section));
					i.custom = parse_custom_section(src.bytes(s->file_offset, n), limits);
					if (s->size > limits.max_custom_section)
						i.custom.issues.push_back({Issue::Severity::error, ErrorCode::limit_exceeded,
												   s->name + " is " + std::to_string(s->size) + " bytes; only the first " +
													   std::to_string(limits.max_custom_section) + " were read"});
				}
			}
		}
	}

	std::size_t find_descriptor(std::span<const std::uint8_t> section, bool* second) noexcept
	{
		constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
		const auto found = std::search(section.begin(), section.end(), kMagic.begin(), kMagic.end());
		if (found == section.end()) return none;
		const std::size_t at = static_cast<std::size_t>(found - section.begin());
		if (second) {
			// Past this descriptor (its own size field, when readable) another magic means a second identity
			std::size_t next = at + kMagic.size();
			if (section.size() - at >= offsets::flags) next = at + std::max<std::size_t>(kMagic.size(), detail::get32(&section[at + offsets::descriptor_size]));
			*second = next < section.size() &&
					  std::search(section.begin() + static_cast<std::ptrdiff_t>(next), section.end(), kMagic.begin(), kMagic.end()) != section.end();
		}
		return at;
	}

	Inspection inspect_bytes(std::span<const std::uint8_t> file, const Limits& limits)
	{
		Inspection i;
		Inspect(MemorySource(file), i, limits);
		return i;
	}

	Inspection inspect_file(const std::filesystem::path& path, const Limits& limits)
	{
		Inspection i;
		FileSource src(path);
		if (!src.ok()) {
			FileError(i, ErrorCode::io, "can't read " + path.string());
			return i;
		}
		Inspect(src, i, limits);
		return i;
	}
}
