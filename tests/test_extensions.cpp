// Standardized extensions: TLV records, unknown-record preservation, bounds, critical records.

#include <memid/memid.hpp>

#include <gtest/gtest.h>

using namespace memid;

namespace
{
	Descriptor WithExtensions(std::vector<Extension> records)
	{
		Descriptor d = DescriptorBuilder{}.mod_id("6f9619ff-8b86-d011-b42d-00c04fc964ff").reverse_domain("com.example.ext").name("Ext").build();
		d.extensions = std::move(records);
		return d;
	}
}

TEST(MemidExtensions, RoundTripKeepsUnknownRecords)
{
	const std::vector<Extension> records = {
		{extension_type::dependencies, 0, {1, 2, 3}},
		{0x9001, 0x0040, {}},						// Private type, unknown flags, empty payload
		{0x1234, 0, std::vector<std::uint8_t>(300, 0xAB)},	// Unknown standard-range type
	};
	const std::vector<std::uint8_t> bytes = serialize(WithExtensions(records));
	EXPECT_EQ(bytes.size(), kDescriptorSize + 3 * kExtensionHeaderSize + 3 + 0 + 300);
	EXPECT_EQ(detail::get32(&bytes[offsets::extension_offset]), kDescriptorSize);
	const ParseResult r = parse(bytes);
	ASSERT_TRUE(r) << r.message;
	EXPECT_EQ(r.descriptor->extensions, records);
	EXPECT_EQ(serialize(*r.descriptor), bytes);	// Byte for byte, unknown records included
	ASSERT_NE(find_extension(r.descriptor->extensions, 0x9001), nullptr);
	EXPECT_EQ(find_extension(r.descriptor->extensions, 0x5555), nullptr);
}

TEST(MemidExtensions, UnknownCriticalRecordIsRefused)
{
	const std::vector<std::uint8_t> bytes = serialize(WithExtensions({{0x7001, extension_flag::critical, {9}}}));
	EXPECT_EQ(parse(bytes).error, ErrorCode::unknown_critical);
	// A known type may be critical
	EXPECT_TRUE(parse(serialize(WithExtensions({{extension_type::relationships, extension_flag::critical, {9}}}))));
}

TEST(MemidExtensions, Bounds)
{
	std::vector<std::uint8_t> bytes = serialize(WithExtensions({{extension_type::dependencies, 0, {1, 2, 3, 4}}}));
	// The record claims more payload than there is
	auto longer = bytes;
	detail::put32(&longer[kDescriptorSize + 4], 0xFFFFFFF0u);
	EXPECT_EQ(parse(longer).error, ErrorCode::truncated);
	// The area claims more bytes than the data has
	auto past = bytes;
	detail::put32(&past[offsets::extension_size], 1000);
	EXPECT_EQ(parse(past).error, ErrorCode::truncated);
	// Offset + size overflowing 32 bits
	auto overflow = bytes;
	detail::put32(&overflow[offsets::extension_offset], 0xFFFFFFF0u);
	detail::put32(&overflow[offsets::extension_size], 0x20);
	EXPECT_NE(parse(overflow).error, ErrorCode::none);
	// Overlapping the descriptor
	auto overlap = bytes;
	detail::put32(&overlap[offsets::extension_offset], 100);
	EXPECT_EQ(parse(overlap).error, ErrorCode::malformed);
	// A header cut short
	EXPECT_EQ(parse_extensions(std::vector<std::uint8_t>{1, 0, 0}).error, ErrorCode::truncated);
	EXPECT_TRUE(parse_extensions({}));
}

TEST(MemidExtensions, Limits)
{
	std::vector<Extension> many(20, Extension{0x9000, 0, {}});
	Limits limits;
	limits.max_extensions = 10;
	EXPECT_EQ(parse_extensions(serialize_extensions(many), limits).error, ErrorCode::limit_exceeded);
	EXPECT_TRUE(parse_extensions(serialize_extensions(many)));
}
