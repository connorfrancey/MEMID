// Binary inspection on synthetic files: PE and ELF (32 and 64 bit) section tables built in memory, missing and
// malformed sections, unsupported formats, and corrupted input (no read may leave the buffer).

#include <memid/memid.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <random>

using namespace memid;

namespace
{
	using Bytes = std::vector<std::uint8_t>;
	struct Section { std::string name; Bytes data; };

	void Put16(Bytes& b, std::size_t at, std::uint16_t v) { detail::put16(&b[at], v); }
	void Put32(Bytes& b, std::size_t at, std::uint32_t v) { detail::put32(&b[at], v); }
	void Put64(Bytes& b, std::size_t at, std::uint64_t v) { detail::put64(&b[at], v); }

	Bytes Identity()
	{
		return serialize(DescriptorBuilder{}.mod_id("6f9619ff-8b86-d011-b42d-00c04fc964ff").reverse_domain("com.example.synthetic")
							 .name("Synthetic").version("3.2.1").build());
	}
	Bytes Custom()
	{
		CustomMetadata m;
		m.add({"com.example.synthetic", MetadataFormat::text, std::string_view("hello")});
		return m.serialize();
	}

	// A PE file: DOS header, NT signature and COFF header (no optional header), section table, then the sections
	Bytes MakePe(const std::vector<Section>& sections, std::uint16_t machine = 0x014C)
	{
		const std::size_t table = 0x40 + 24, dataStart = table + sections.size() * 40;
		Bytes b(dataStart);
		b[0] = 'M', b[1] = 'Z';
		Put32(b, 0x3C, 0x40);
		std::memcpy(&b[0x40], "PE\0\0", 4);
		Put16(b, 0x44, machine);
		Put16(b, 0x46, static_cast<std::uint16_t>(sections.size()));
		for (std::size_t i = 0; i < sections.size(); ++i) {
			const std::size_t h = table + i * 40;
			std::memcpy(&b[h], sections[i].name.data(), std::min<std::size_t>(8, sections[i].name.size()));
			Put32(b, h + 8, static_cast<std::uint32_t>(sections[i].data.size()));		// VirtualSize
			Put32(b, h + 16, static_cast<std::uint32_t>(sections[i].data.size() + 16));	// SizeOfRawData: padded
			Put32(b, h + 20, static_cast<std::uint32_t>(b.size()));						// PointerToRawData
			b.insert(b.end(), sections[i].data.begin(), sections[i].data.end());
			b.insert(b.end(), 16, 0);
		}
		return b;
	}

	// An ELF file with a section name table
	Bytes MakeElf(const std::vector<Section>& sections, bool is64)
	{
		std::string names(1, '\0');
		std::vector<std::uint32_t> nameAt;
		for (const Section& s : sections) nameAt.push_back(static_cast<std::uint32_t>(names.size())), names += s.name + '\0';
		const std::uint32_t strtabName = static_cast<std::uint32_t>(names.size());
		names += ".shstrtab";
		names += '\0';

		const std::size_t header = is64 ? 64 : 52, entSize = is64 ? 64 : 40;
		Bytes b(header);
		b[0] = 0x7F, b[1] = 'E', b[2] = 'L', b[3] = 'F', b[4] = is64 ? 2 : 1, b[5] = 1, b[6] = 1;
		Put16(b, 18, is64 ? 62 : 3);
		struct Placed { std::uint32_t name; std::uint64_t offset, size; };
		std::vector<Placed> placed{{0, 0, 0}};	// Section 0 is the null section
		for (std::size_t i = 0; i < sections.size(); ++i) {
			placed.push_back({nameAt[i], b.size(), sections[i].data.size()});
			b.insert(b.end(), sections[i].data.begin(), sections[i].data.end());
		}
		placed.push_back({strtabName, b.size(), names.size()});
		b.insert(b.end(), names.begin(), names.end());
		const std::size_t shoff = b.size();
		b.resize(shoff + placed.size() * entSize, 0);
		for (std::size_t i = 0; i < placed.size(); ++i) {
			const std::size_t h = shoff + i * entSize;
			Put32(b, h, placed[i].name);
			Put32(b, h + 4, i == 0 ? 0 : 1);	// SHT_PROGBITS
			if (is64) {
				Put64(b, h + 24, placed[i].offset);
				Put64(b, h + 32, placed[i].size);
			} else {
				Put32(b, h + 16, static_cast<std::uint32_t>(placed[i].offset));
				Put32(b, h + 20, static_cast<std::uint32_t>(placed[i].size));
			}
		}
		if (is64) {
			Put64(b, 40, shoff);
			Put16(b, 58, static_cast<std::uint16_t>(entSize));
			Put16(b, 60, static_cast<std::uint16_t>(placed.size()));
			Put16(b, 62, static_cast<std::uint16_t>(placed.size() - 1));
		} else {
			Put32(b, 32, static_cast<std::uint32_t>(shoff));
			Put16(b, 46, static_cast<std::uint16_t>(entSize));
			Put16(b, 48, static_cast<std::uint16_t>(placed.size()));
			Put16(b, 50, static_cast<std::uint16_t>(placed.size() - 1));
		}
		return b;
	}

	Bytes Padded(Bytes b, std::size_t before)
	{
		b.insert(b.begin(), before, 0);
		return b;
	}
}

TEST(MemidInspector, PeWithIdentityAndCustomMetadata)
{
	const Inspection i = inspect_bytes(MakePe({{".text", Bytes(32, 0xCC)}, {".memid", Padded(Identity(), 16)}, {".memidx", Custom()}}));
	EXPECT_EQ(i.file_error, ErrorCode::none) << i.file_message;
	EXPECT_EQ(i.format, BinaryFormat::pe);
	EXPECT_EQ(i.architecture, Architecture::x86);
	ASSERT_TRUE(i.descriptor) << i.identity_message;
	EXPECT_TRUE(i.identified());
	EXPECT_EQ(i.descriptor->name, "Synthetic");
	EXPECT_EQ(i.identity_bytes, Identity());
	EXPECT_TRUE(i.has_custom_section);
	ASSERT_EQ(i.custom.metadata.size(), 1u);
	EXPECT_EQ(i.custom.metadata.entries()[0].text(), "hello");
}

TEST(MemidInspector, ElfBothClasses)
{
	for (const bool is64 : {false, true}) {
		const Inspection i = inspect_bytes(MakeElf({{".text", Bytes(8, 0x90)}, {".memid", Identity()}, {".memidx", Custom()}}, is64));
		EXPECT_EQ(i.format, BinaryFormat::elf) << is64;
		EXPECT_EQ(i.architecture, is64 ? Architecture::x86_64 : Architecture::x86);
		ASSERT_TRUE(i.descriptor) << i.identity_message << i.file_message;
		EXPECT_EQ(i.descriptor->version, (Version{3, 2, 1}));
		EXPECT_EQ(i.custom.metadata.size(), 1u);
	}
}

TEST(MemidInspector, MissingAndBrokenIdentity)
{
	// No .memid: a legacy binary, not an error in the file
	const Inspection none = inspect_bytes(MakePe({{".text", Bytes(8)}}));
	EXPECT_EQ(none.file_error, ErrorCode::none);
	EXPECT_EQ(none.identity_error, ErrorCode::not_found);
	EXPECT_FALSE(none.has_identity_section);

	// An empty .memid
	EXPECT_EQ(inspect_bytes(MakePe({{".memid", Bytes(32)}})).identity_error, ErrorCode::not_found);

	// A malformed descriptor, while the custom metadata stays readable
	Bytes broken = Identity();
	detail::put32(&broken[offsets::descriptor_size], 12);
	const Inspection bad = inspect_bytes(MakePe({{".memid", broken}, {".memidx", Custom()}}));
	EXPECT_EQ(bad.identity_error, ErrorCode::malformed);
	EXPECT_FALSE(bad.descriptor);
	EXPECT_EQ(bad.custom.metadata.size(), 1u);

	// Two descriptors in one binary
	Bytes two = Identity();
	const Bytes again = Identity();
	two.insert(two.end(), again.begin(), again.end());
	EXPECT_EQ(inspect_bytes(MakePe({{".memid", two}})).identity_error, ErrorCode::malformed);

	// A malformed .memidx doesn't touch the identity
	Bytes customBad = Custom();
	customBad[20] = 1;
	detail::put32(&customBad[12], 9999);
	const Inspection cb = inspect_bytes(MakePe({{".memid", Identity()}, {".memidx", customBad}}));
	EXPECT_TRUE(cb.identified());
	EXPECT_FALSE(cb.custom.ok());
}

TEST(MemidInspector, SectionsOutsideTheFile)
{
	Bytes pe = MakePe({{".memid", Identity()}});
	Put32(pe, 0x58 + 20, 0x7FFFFFF0);	// The section's raw data pointer, far past the end
	const Inspection i = inspect_bytes(pe);
	EXPECT_EQ(i.identity_error, ErrorCode::malformed);

	Bytes table = MakePe({{".memid", Identity()}});
	Put16(table, 0x46, 0xFFFF);	// More sections than the file holds
	EXPECT_EQ(inspect_bytes(table).file_error, ErrorCode::malformed);

	Bytes nt = MakePe({});
	Put32(nt, 0x3C, 0xFFFFFFF0);	// NT headers past the end
	EXPECT_EQ(inspect_bytes(nt).file_error, ErrorCode::malformed);
}

TEST(MemidInspector, UnsupportedFormats)
{
	EXPECT_EQ(inspect_bytes({}).file_error, ErrorCode::unsupported_binary);
	EXPECT_EQ(inspect_bytes(Bytes{'h', 'e', 'l', 'l', 'o'}).file_error, ErrorCode::unsupported_binary);
	const Inspection macho = inspect_bytes(Bytes{0xCF, 0xFA, 0xED, 0xFE, 0, 0, 0, 0});
	EXPECT_EQ(macho.format, BinaryFormat::macho);
	EXPECT_EQ(macho.file_error, ErrorCode::unsupported_binary);
	Bytes elfBig = MakeElf({{".memid", Identity()}}, true);
	elfBig[5] = 2;	// Big-endian
	EXPECT_EQ(inspect_bytes(elfBig).file_error, ErrorCode::unsupported_binary);
	EXPECT_EQ(inspect_file("this/file/does/not/exist.dll").file_error, ErrorCode::io);
}

// Every prefix of a valid file, and random byte flips: the inspector must report, never read out of bounds or throw
TEST(MemidInspector, CorruptedInputIsSafe)
{
	const Bytes pe = MakePe({{".text", Bytes(64, 0xCC)}, {".memid", Identity()}, {".memidx", Custom()}});
	const Bytes elf = MakeElf({{".memid", Identity()}, {".memidx", Custom()}}, true);
	for (const Bytes* file : {&pe, &elf}) {
		for (std::size_t n = 0; n < file->size(); n += 3) EXPECT_NO_THROW((void)inspect_bytes(std::span(*file).first(n)));
		std::mt19937 rng(1234);
		for (int round = 0; round < 3000; ++round) {
			Bytes b = *file;
			for (int k = 0; k < 4; ++k) b[rng() % b.size()] = static_cast<std::uint8_t>(rng());
			EXPECT_NO_THROW((void)inspect_bytes(b));
		}
	}
}

TEST(MemidInspector, FindDescriptorSkipsPadding)
{
	bool second = true;
	const Bytes padded = Padded(Identity(), 64);
	EXPECT_EQ(find_descriptor(padded, &second), 64u);
	EXPECT_FALSE(second);
	EXPECT_EQ(find_descriptor(Bytes(100), nullptr), std::numeric_limits<std::size_t>::max());
}
