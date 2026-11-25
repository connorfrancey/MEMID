// The .memidx container: entries, namespaces, round trips, padding between containers, malformed input and limits.

#include <memid/embed.hpp>
#include <memid/memid.hpp>

#include <gtest/gtest.h>

using namespace memid;

namespace
{
	CustomMetadata Sample()
	{
		CustomMetadata m;
		m.add({"com.example.mod", MetadataFormat::json, std::string_view(R"({"a":1})")});
		m.add({"com.example.mod.binary", MetadataFormat::raw, std::vector<std::uint8_t>{0, 1, 2, 0xFF}});
		m.add({"com.example.mod", MetadataFormat::text, std::string_view("second entry, same namespace")});
		m.add({"org.other.tool", static_cast<MetadataFormat>(0x9001), std::vector<std::uint8_t>{}});	// Private format, empty
		return m;
	}
}

TEST(MemidCustom, RoundTrip)
{
	const CustomMetadata m = Sample();
	EXPECT_TRUE(m.validate().ok());
	const std::vector<std::uint8_t> bytes = m.serialize();
	EXPECT_EQ(detail::get32(&bytes[12]), bytes.size());
	EXPECT_EQ(detail::get32(&bytes[16]), 4u);
	const CustomParse p = parse_custom_section(bytes);
	EXPECT_TRUE(p.ok());
	EXPECT_EQ(p.containers, 1u);
	EXPECT_EQ(p.metadata.entries(), m.entries());
	EXPECT_EQ(p.metadata.serialize(), bytes);
}

TEST(MemidCustom, NamespaceLookup)
{
	CustomMetadata m = Sample();
	ASSERT_NE(m.find("com.example.mod"), nullptr);
	EXPECT_EQ(m.find("com.example.mod")->text(), R"({"a":1})");	// The first of two
	EXPECT_EQ(m.find_all("com.example.mod").size(), 2u);
	EXPECT_EQ(m.find("com.example.nothing"), nullptr);
	m.replace({"com.example.mod", MetadataFormat::text, std::string_view("only one now")});
	EXPECT_EQ(m.find_all("com.example.mod").size(), 1u);
	EXPECT_EQ(m.find("com.example.mod")->text(), "only one now");
	EXPECT_EQ(m.remove("org.other.tool"), 1u);
	EXPECT_EQ(m.size(), 2u);
}

TEST(MemidCustom, ValidationOfEntries)
{
	CustomMetadata m;
	m.add({"NotADomain", MetadataFormat::raw, std::vector<std::uint8_t>{1}});
	m.add({"com.example.text", MetadataFormat::json, std::vector<std::uint8_t>{0xC0, 0x80}});	// JSON that isn't UTF-8
	EXPECT_EQ(m.validate().issues.size(), 2u);
}

TEST(MemidCustom, ContainersWithPaddingBetween)
{
	CustomMetadata a, b;
	a.add({"com.example.a", MetadataFormat::text, std::string_view("A")});
	b.add({"com.example.b", MetadataFormat::text, std::string_view("B")});
	std::vector<std::uint8_t> section = a.serialize();
	section.insert(section.end(), 13, 0);	// Linker padding
	const auto second = b.serialize();
	section.insert(section.end(), second.begin(), second.end());
	section.insert(section.end(), 7, 0);
	const CustomParse p = parse_custom_section(section);
	EXPECT_TRUE(p.ok());
	EXPECT_EQ(p.containers, 2u);
	ASSERT_EQ(p.metadata.size(), 2u);
	EXPECT_EQ(p.metadata.entries()[1].text(), "B");
}

TEST(MemidCustom, MalformedInputKeepsWhatWasRead)
{
	const std::vector<std::uint8_t> good = Sample().serialize();
	EXPECT_TRUE(parse_custom_section({}).ok());	// No section content is valid

	// Garbage after a valid container: the container's entries stay
	auto garbage = good;
	garbage.push_back('Z');
	const CustomParse g = parse_custom_section(garbage);
	EXPECT_FALSE(g.ok());
	EXPECT_EQ(g.metadata.size(), 4u);

	// A container cut short
	auto cut = std::vector<std::uint8_t>(good.begin(), good.end() - 3);
	EXPECT_EQ(parse_custom_section(cut).issues.front().code, ErrorCode::malformed);

	// An entry whose payload size runs past its container
	auto payload = good;
	detail::put32(&payload[kCustomHeaderSize + 4], 0xFFFFFF00u);
	const CustomParse pp = parse_custom_section(payload);
	EXPECT_FALSE(pp.ok());
	EXPECT_EQ(pp.metadata.size(), 0u);

	// A bad namespace skips that entry only
	auto ns = good;
	ns[kCustomHeaderSize + kCustomEntryHeaderSize] = 'C';	// "Com.example.mod"
	const CustomParse bad = parse_custom_section(ns);
	EXPECT_FALSE(bad.ok());
	EXPECT_EQ(bad.metadata.size(), 3u);

	// An unknown container version is skipped by its size
	auto version = good;
	detail::put16(&version[8], 9);
	const CustomParse v = parse_custom_section(version);
	EXPECT_EQ(v.metadata.size(), 0u);
	EXPECT_EQ(v.issues.front().code, ErrorCode::unsupported_version);

	// A header size below 24
	auto header = good;
	detail::put16(&header[10], 4);
	EXPECT_FALSE(parse_custom_section(header).ok());
}

TEST(MemidCustom, Limits)
{
	CustomMetadata m;
	for (int i = 0; i < 50; ++i) m.add({"com.example.many", MetadataFormat::raw, std::vector<std::uint8_t>(10, 1)});
	const auto bytes = m.serialize();
	Limits limits;
	limits.max_entries = 20;
	EXPECT_FALSE(parse_custom_section(bytes, limits).ok());
	limits = {};
	limits.max_payload = 5;
	EXPECT_EQ(parse_custom_section(bytes, limits).metadata.size(), 0u);
	limits = {};
	limits.max_custom_section = 100;
	EXPECT_FALSE(parse_custom_section(bytes, limits).ok());
	limits = {};
	limits.max_namespace = 4;
	EXPECT_EQ(parse_custom_section(bytes, limits).metadata.size(), 0u);
}

TEST(MemidCustom, CompileTimeContainerMatchesSerialize)
{
	constexpr auto text = detail::custom_text_container("com.example.mod", static_cast<std::uint16_t>(MetadataFormat::json), R"({"a":1})");
	CustomMetadata m;
	m.add({"com.example.mod", MetadataFormat::json, std::string_view(R"({"a":1})")});
	const auto runtime = m.serialize();
	EXPECT_TRUE(std::equal(text.begin(), text.end(), runtime.begin(), runtime.end()));

	static constexpr unsigned char bytes[] = {9, 8, 7};
	constexpr auto raw = detail::custom_bytes_container("com.example.raw", 0, bytes);
	const CustomParse p = parse_custom_section(raw);
	ASSERT_EQ(p.metadata.size(), 1u);
	EXPECT_EQ(p.metadata.entries()[0].payload, (std::vector<std::uint8_t>{9, 8, 7}));
}
