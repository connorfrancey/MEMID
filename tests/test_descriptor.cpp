// The standardized descriptor: layout (size, every offset, against schemas/memid-v1.1-layout.json), round trips,
// structural errors, validation, UUIDs, versions, compatibility, and compile-time embedding matching run-time output.

#include <memid/embed.hpp>
#include <memid/memid.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <sstream>

using namespace memid;

namespace
{
	Descriptor Sample()
	{
		return DescriptorBuilder{}
			.mod_id("6f9619ff-8b86-d011-b42d-00c04fc964ff")
			.publisher_id("a3bb189e-8bf9-3888-9912-ace4e6543002")
			.build_id("0f8fad5b-d9cb-469f-a165-70867728950e")
			.reverse_domain("com.example.wallrun-trails")
			.name("Wallrun Trails")
			.publisher("Example Publisher")
			.homepage("https://example.com/trails")
			.version("1.2.3")
			.channel(ReleaseChannel::beta)
			.architecture(Architecture::x86)
			.os(OperatingSystem::windows)
			.binary_format(BinaryFormat::pe)
			.language(Language::cpp)
			.runtime(Runtime::mirage)
			.build_timestamp(1764000000)
			.game(game::mirrors_edge_2008_pc, pack_game_version(1, 0, 0), pack_game_version(1, 1, 0))
			.interop(1, 4)
			.capabilities(capability::gameplay | capability::rendering)
			.protocol_flags(protocol::mirage_api)
			.flags(flags::has_custom_metadata)
			.build();
	}

	std::vector<std::uint8_t> Bytes(const Descriptor& d) { return serialize(d); }
}

TEST(MemidLayout, DescriptorIs428Bytes)
{
	EXPECT_EQ(kDescriptorSize, 428u);
	EXPECT_EQ(Bytes(Sample()).size(), 428u);
	EXPECT_EQ(offsets::extension_flags + 4, kDescriptorSize);
}

TEST(MemidLayout, OffsetsMatchTheSchema)
{
	std::ifstream in(MEMID_SCHEMA_FILE);
	ASSERT_TRUE(in) << MEMID_SCHEMA_FILE;
	std::stringstream text;
	text << in.rdbuf();
	const std::string json = text.str();
	std::map<std::string, std::size_t> schema;
	const std::regex pair("\"([a-z_]+)\"\\s*:\\s*([0-9]+)");
	const auto offsetsAt = json.find("\"offsets\"");
	ASSERT_NE(offsetsAt, std::string::npos);
	const auto offsetsEnd = json.find('}', offsetsAt);	// The offsets object only (sizes follow)
	for (std::sregex_iterator it(json.begin() + static_cast<std::ptrdiff_t>(offsetsAt), json.begin() + static_cast<std::ptrdiff_t>(offsetsEnd), pair), end;
		 it != end; ++it)
		schema[(*it)[1]] = std::stoul((*it)[2]);
	const std::map<std::string, std::size_t> ours = {
		{"magic_bytes", offsets::magic}, {"format_major", offsets::format_major}, {"format_minor", offsets::format_minor},
		{"descriptor_size", offsets::descriptor_size}, {"flags", offsets::flags}, {"mod_id", offsets::mod_id},
		{"publisher_id", offsets::publisher_id}, {"version_major", offsets::version_major},
		{"release_channel", offsets::release_channel}, {"architecture", offsets::architecture}, {"build_id", offsets::build_id},
		{"build_timestamp", offsets::build_timestamp}, {"toolchain_id", offsets::toolchain_id}, {"game_id", offsets::game_id},
		{"capabilities", offsets::capabilities}, {"protocol_flags", offsets::protocol_flags},
		{"reverse_domain", offsets::reverse_domain}, {"name", offsets::name}, {"publisher", offsets::publisher},
		{"homepage", offsets::homepage}, {"extension_offset", offsets::extension_offset},
		{"extension_size", offsets::extension_size}, {"extension_flags", offsets::extension_flags},
		// The fields the 1.1 schema leaves unnamed (proposed 2026-11-25)
		{"version_minor", offsets::version_minor}, {"version_patch", offsets::version_patch},
		{"operating_system", offsets::operating_system}, {"binary_format", offsets::binary_format}, {"language", offsets::language},
		{"runtime", offsets::runtime}, {"game_version_min", offsets::game_version_min}, {"game_version_max", offsets::game_version_max},
		{"interop_min", offsets::interop_min}, {"interop_max", offsets::interop_max}, {"reserved", offsets::reserved}};
	for (const auto& [name, at] : ours) {
		const auto it = schema.find(name);
		ASSERT_NE(it, schema.end()) << name << " is missing from the schema";
		EXPECT_EQ(it->second, at) << name;
	}
	EXPECT_EQ(schema.size(), ours.size()) << "the schema names a field this library doesn't";
}

TEST(MemidLayout, FieldsAreWrittenWhereTheLayoutSays)
{
	const std::vector<std::uint8_t> b = Bytes(Sample());
	const auto u16 = [&](std::size_t at) { return static_cast<unsigned>(b[at] | b[at + 1] << 8); };
	const auto u32 = [&](std::size_t at) { return detail::get32(&b[at]); };
	EXPECT_TRUE(std::equal(kMagic.begin(), kMagic.end(), b.begin()));
	EXPECT_EQ(u16(offsets::format_major), 1u);
	EXPECT_EQ(u16(offsets::format_minor), 1u);
	EXPECT_EQ(u32(offsets::descriptor_size), 428u);
	EXPECT_EQ(u32(offsets::flags), flags::has_custom_metadata);
	EXPECT_EQ(b[offsets::mod_id], 0x6F);
	EXPECT_EQ(b[offsets::mod_id + 15], 0xFF);
	EXPECT_EQ(b[offsets::publisher_id], 0xA3);
	EXPECT_EQ(u16(offsets::version_major), 1u);
	EXPECT_EQ(u16(offsets::version_minor), 2u);
	EXPECT_EQ(u16(offsets::version_patch), 3u);
	EXPECT_EQ(u16(offsets::release_channel), static_cast<unsigned>(ReleaseChannel::beta));
	EXPECT_EQ(u16(offsets::architecture), static_cast<unsigned>(Architecture::x86));
	EXPECT_EQ(u16(offsets::operating_system), static_cast<unsigned>(OperatingSystem::windows));
	EXPECT_EQ(b[offsets::binary_format], static_cast<unsigned>(BinaryFormat::pe));
	EXPECT_EQ(b[offsets::language], static_cast<unsigned>(Language::cpp));
	EXPECT_EQ(u16(offsets::runtime), static_cast<unsigned>(Runtime::mirage));
	EXPECT_EQ(b[offsets::build_id], 0x0F);
	EXPECT_EQ(detail::get64(&b[offsets::build_timestamp]), 1764000000u);
	EXPECT_EQ(u32(offsets::game_id), game::mirrors_edge_2008_pc);
	EXPECT_EQ(u32(offsets::game_version_min), pack_game_version(1, 0, 0));
	EXPECT_EQ(u32(offsets::game_version_max), pack_game_version(1, 1, 0));
	EXPECT_EQ(u32(offsets::interop_min), 1u);
	EXPECT_EQ(u32(offsets::interop_max), 4u);
	for (std::size_t i = 0; i < field_size::reserved; ++i) EXPECT_EQ(b[offsets::reserved + i], 0);
	EXPECT_EQ(detail::get64(&b[offsets::capabilities]), capability::gameplay | capability::rendering);
	EXPECT_EQ(detail::get64(&b[offsets::protocol_flags]), protocol::mirage_api);
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(&b[offsets::reverse_domain])), "com.example.wallrun-trails");
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(&b[offsets::name])), "Wallrun Trails");
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(&b[offsets::publisher])), "Example Publisher");
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(&b[offsets::homepage])), "https://example.com/trails");
	EXPECT_EQ(u32(offsets::extension_offset), 0u);
	EXPECT_EQ(u32(offsets::extension_size), 0u);
}

TEST(MemidDescriptor, RoundTrips)
{
	const Descriptor d = Sample();
	const ParseResult r = parse(Bytes(d));
	ASSERT_TRUE(r) << r.message;
	EXPECT_EQ(*r.descriptor, d);
	EXPECT_TRUE(r.warnings.empty());
	EXPECT_TRUE(validate(d).ok());
}

TEST(MemidDescriptor, TextFillingItsWholeFieldRoundTrips)
{
	Descriptor d = Sample();
	d.name = std::string(64, 'n');
	d.publisher = std::string(48, 'p');
	const ParseResult r = parse(Bytes(d));
	ASSERT_TRUE(r);
	EXPECT_EQ(r.descriptor->name, d.name);
	EXPECT_EQ(r.descriptor->publisher, d.publisher);
}

TEST(MemidDescriptor, StructuralErrors)
{
	std::vector<std::uint8_t> good = Bytes(Sample());
	EXPECT_EQ(parse({}).error, ErrorCode::truncated);
	EXPECT_EQ(parse(std::span(good).first(427)).error, ErrorCode::truncated);

	auto magic = good;
	magic[0] = 'X';
	EXPECT_EQ(parse(magic).error, ErrorCode::bad_magic);

	auto major = good;
	detail::put16(&major[offsets::format_major], 2);
	EXPECT_EQ(parse(major).error, ErrorCode::unsupported_version);

	auto small = good;
	detail::put32(&small[offsets::descriptor_size], 100);
	EXPECT_EQ(parse(small).error, ErrorCode::malformed);

	auto big = good;
	detail::put32(&big[offsets::descriptor_size], 0xFFFFFFFFu);
	EXPECT_EQ(parse(big).error, ErrorCode::limit_exceeded);

	auto hidden = good;	// A byte after a text's NUL terminator
	hidden[offsets::name + 60] = 'x';
	EXPECT_EQ(parse(hidden).error, ErrorCode::malformed);

	auto utf8 = good;
	utf8[offsets::name] = 0xC0;	// An overlong form
	utf8[offsets::name + 1] = 0x80;
	EXPECT_EQ(parse(utf8).error, ErrorCode::invalid_field);
}

TEST(MemidDescriptor, NewerMinorAndReservedBytesAreWarnings)
{
	auto bytes = Bytes(Sample());
	detail::put16(&bytes[offsets::format_minor], 7);
	bytes[offsets::reserved] = 1;
	bytes.resize(440, 0);
	detail::put32(&bytes[offsets::descriptor_size], 440);	// A later 1.x with 12 more bytes
	const ParseResult r = parse(bytes);
	ASSERT_TRUE(r) << r.message;
	EXPECT_EQ(r.warnings.size(), 2u);
	EXPECT_EQ(r.descriptor->format_minor, 7);
}

TEST(MemidDescriptor, Validation)
{
	EXPECT_TRUE(validate(Sample()).ok());
	auto check = [](auto change) {
		Descriptor d = Sample();
		change(d);
		return validate(d).ok();
	};
	EXPECT_FALSE(check([](Descriptor& d) { d.mod_id = {}; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.reverse_domain = "Example.Mod"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.reverse_domain = "nodots"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.reverse_domain = "com..example"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.reverse_domain = "com.-example"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.reverse_domain = std::string(90, 'a') + ".example"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.name.clear(); }));
	EXPECT_FALSE(check([](Descriptor& d) { d.name = std::string(65, 'n'); }));
	EXPECT_FALSE(check([](Descriptor& d) { d.name = " padded"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.name = "tab\there"; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.publisher = std::string(49, 'p'); }));
	EXPECT_FALSE(check([](Descriptor& d) { d.game_version_min = 5, d.game_version_max = 4; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.interop_min = 9, d.interop_max = 2; }));
	EXPECT_FALSE(check([](Descriptor& d) { d.extensions.push_back({0x7777, extension_flag::critical, {1}}); }));
	EXPECT_TRUE(check([](Descriptor& d) { d.extensions.push_back({0x7777, 0, {1}}); }));	// Unknown, not critical
	EXPECT_TRUE(check([](Descriptor& d) { d.publisher.clear(), d.homepage.clear(), d.publisher_id = {}; }));
	EXPECT_TRUE(check([](Descriptor& d) { d.name = "Ünïcødé 名前"; }));
	{
		Descriptor d = Sample();
		d.homepage = "example.com";	// Allowed, but warned about
		const ValidationResult v = validate(d);
		EXPECT_TRUE(v.ok());
		EXPECT_EQ(v.issues.size(), 1u);
	}
}

TEST(MemidDescriptor, LongTextIsCutAtACharacterBoundary)
{
	Descriptor d = Sample();
	d.name = std::string(63, 'a') + "é";	// 65 bytes: the two-byte é doesn't fit
	const ParseResult r = parse(Bytes(d));
	ASSERT_TRUE(r);
	EXPECT_EQ(r.descriptor->name, std::string(63, 'a'));
}

TEST(MemidDescriptor, BuilderReportsValuesThatDontParse)
{
	Descriptor out;
	const ValidationResult v = DescriptorBuilder{}.mod_id("not-a-uuid").version("1.2").reverse_domain("com.example.x").name("X").build_checked(out);
	EXPECT_FALSE(v.ok());
	EXPECT_GE(v.issues.size(), 3u);	// The mod id, the version, and the then-missing mod id
}

TEST(MemidDescriptor, Compatibility)
{
	Descriptor d = Sample();
	EXPECT_TRUE(supports_architecture(d, Architecture::x86));
	EXPECT_FALSE(supports_architecture(d, Architecture::x86_64));
	d.architecture = Architecture::any;
	EXPECT_TRUE(supports_architecture(d, Architecture::x86_64));
	EXPECT_TRUE(supports_os(d, OperatingSystem::windows));
	EXPECT_FALSE(supports_os(d, OperatingSystem::linux_));
	EXPECT_TRUE(supports_game(d, game::mirrors_edge_2008_pc, pack_game_version(1, 0, 0)));
	EXPECT_TRUE(supports_game(d, game::mirrors_edge_2008_pc, pack_game_version(1, 1, 0)));
	EXPECT_FALSE(supports_game(d, game::mirrors_edge_2008_pc, pack_game_version(1, 2, 0)));
	EXPECT_FALSE(supports_game(d, 99, pack_game_version(1, 0, 0)));
	d.game_id = game::none;
	EXPECT_TRUE(supports_game(d, 99, 0));
	EXPECT_TRUE(supports_interop(d, 1));
	EXPECT_TRUE(supports_interop(d, 4));
	EXPECT_FALSE(supports_interop(d, 5));
	d.interop_max = 0;
	EXPECT_TRUE(supports_interop(d, 500));
	EXPECT_TRUE(supports_format(d));
}

TEST(MemidUuid, ParseAndFormat)
{
	const auto u = Uuid::parse("6F9619FF-8B86-D011-B42D-00C04FC964FF");
	ASSERT_TRUE(u);
	EXPECT_EQ(u->to_string(), "6f9619ff-8b86-d011-b42d-00c04fc964ff");
	EXPECT_FALSE(u->nil());
	EXPECT_TRUE(Uuid{}.nil());
	EXPECT_FALSE(Uuid::parse("6f9619ff8b86d011b42d00c04fc964ff"));
	EXPECT_FALSE(Uuid::parse("{6f9619ff-8b86-d011-b42d-00c04fc964ff}"));
	EXPECT_FALSE(Uuid::parse("6f9619ff-8b86-d011-b42d-00c04fc964fg"));
	EXPECT_FALSE(Uuid::parse("6f9619ff-8b86-d011-b42d_00c04fc964ff"));
	EXPECT_LT(*Uuid::parse("00000000-0000-0000-0000-000000000001"), *u);
	EXPECT_EQ(UuidHash{}(*u), UuidHash{}(*Uuid::parse(u->to_string())));
}

TEST(MemidVersion, Parse)
{
	EXPECT_EQ(Version::parse("1.2.3"), (Version{1, 2, 3}));
	EXPECT_EQ(Version::parse("0.0.0"), (Version{}));
	EXPECT_EQ(Version::parse("65535.0.1"), (Version{65535, 0, 1}));
	for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.3-beta", "65536.0.0", "a.b.c", "1..3", " 1.2.3"})
		EXPECT_FALSE(Version::parse(bad)) << bad;
	EXPECT_LT((Version{1, 2, 3}), (Version{1, 10, 0}));
}

TEST(MemidUtf8, Validation)
{
	EXPECT_TRUE(is_utf8("plain"));
	EXPECT_TRUE(is_utf8("\xE2\x82\xAC"));			// €
	EXPECT_TRUE(is_utf8("\xF0\x9F\x98\x80"));		// U+1F600
	EXPECT_FALSE(is_utf8("\xC0\xAF"));				// Overlong
	EXPECT_FALSE(is_utf8("\xED\xA0\x80"));			// A surrogate
	EXPECT_FALSE(is_utf8("\xF4\x90\x80\x80"));		// Past U+10FFFF
	EXPECT_FALSE(is_utf8("\xE2\x82"));				// Truncated
	EXPECT_FALSE(is_utf8("\x80"));
}

// The descriptor MEMID_DEFINE_IDENTITY builds at compile time is the one serialize() writes at run time
TEST(MemidEmbed, CompileTimeEncodingMatchesSerialize)
{
	constexpr detail::Bytes embedded = embed::encode({
		.mod_id = "6f9619ff-8b86-d011-b42d-00c04fc964ff",
		.reverse_domain = "com.example.wallrun-trails",
		.name = "Wallrun Trails",
		.publisher = "Example Publisher",
		.version = "1.2.3",
		.homepage = "https://example.com/trails",
		.publisher_id = "a3bb189e-8bf9-3888-9912-ace4e6543002",
		.build_id = "0f8fad5b-d9cb-469f-a165-70867728950e",
		.channel = ReleaseChannel::beta,
		.architecture = Architecture::x86,
		.os = OperatingSystem::windows,
		.binary_format = BinaryFormat::pe,
		.language = Language::cpp,
		.runtime = Runtime::mirage,
		.game_id = game::mirrors_edge_2008_pc,
		.game_version_min = pack_game_version(1, 0, 0),
		.game_version_max = pack_game_version(1, 1, 0),
		.interop_min = 1,
		.interop_max = 4,
		.capabilities = capability::gameplay | capability::rendering,
		.protocol_flags = protocol::mirage_api,
		.flags = flags::has_custom_metadata,
		.build_timestamp = 1764000000,
		.toolchain_id = 0,
	});
	static_assert(embedded[0] == 'M' && embedded[5] == 0x0D);
	Descriptor d = Sample();
	d.toolchain_id = 0;
	d.protocol_flags |= protocol::c_abi_getter;	// encode() always declares the getter
	const std::vector<std::uint8_t> runtime = serialize(d);
	EXPECT_TRUE(std::equal(embedded.begin(), embedded.end(), runtime.begin(), runtime.end()));
}
