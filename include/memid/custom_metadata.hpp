// Developer-defined custom metadata: the .memidx section, separate from the standardized .memid descriptor.
//
// A .memidx section holds one or more containers back to back (each MEMID_DEFINE_CUSTOM_* use adds one; the linker may
// put zero padding between them). A container is a 24-byte header (magic "MEMIDX\r\n", version u16, header size u16,
// total size u32, entry count u32, reserved u32) followed by entries: namespace size u16, format u16, payload size u32,
// flags u16, reserved u16, the namespace (UTF-8, not terminated), the payload. Little-endian, no padding.
//
//   memid::CustomMetadata m;
//   m.add({"com.example.mymod", memid::MetadataFormat::json, bytes});
//   if (const auto* e = m.find("com.example.mymod")) use(e->payload);
//
// MEMID defines the container, not the payloads: readers only interpret namespaces they know, and nothing here makes a
// payload trusted.
#pragma once

#include <memid/detail/codec.hpp>
#include <memid/diagnostics.hpp>
#include <memid/export.hpp>
#include <memid/format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace memid
{
	struct CustomEntry
	{
		std::string namespace_id;			// Reverse-domain, e.g. "com.example.mymod"
		std::uint16_t format = static_cast<std::uint16_t>(MetadataFormat::raw);	// MetadataFormat, or >= 0x8000: private
		std::vector<std::uint8_t> payload;
		std::uint16_t flags = 0;			// No bits defined in version 1: kept as read

		CustomEntry() = default;
		CustomEntry(std::string ns, MetadataFormat f, std::vector<std::uint8_t> bytes, std::uint16_t entry_flags = 0)
			: namespace_id(std::move(ns)), format(static_cast<std::uint16_t>(f)), payload(std::move(bytes)), flags(entry_flags) {}
		CustomEntry(std::string ns, MetadataFormat f, std::string_view text)
			: namespace_id(std::move(ns)), format(static_cast<std::uint16_t>(f)), payload(text.begin(), text.end()) {}

		std::string_view text() const { return {reinterpret_cast<const char*>(payload.data()), payload.size()}; }
		friend bool operator==(const CustomEntry&, const CustomEntry&) = default;
	};

	// Whether `ns` may name custom metadata: a reverse-domain identifier (memid::is_reverse_domain) of at most `max` bytes
	MEMID_API bool is_namespace(std::string_view ns, std::size_t max = 128) noexcept;

	// Entries in a fixed order. Several entries may share a namespace; find() returns the first.
	class MEMID_API CustomMetadata
	{
	public:
		void add(CustomEntry entry) { entries_.push_back(std::move(entry)); }
		// Every entry of `ns` replaced by `entry` (added if there were none)
		void replace(CustomEntry entry);
		// Every entry of `ns` removed; how many there were
		std::size_t remove(std::string_view ns);

		const CustomEntry* find(std::string_view ns) const noexcept;
		std::vector<const CustomEntry*> find_all(std::string_view ns) const;
		const std::vector<CustomEntry>& entries() const noexcept { return entries_; }
		std::size_t size() const noexcept { return entries_.size(); }
		bool empty() const noexcept { return entries_.empty(); }

		// Problems that would stop serialize() or that readers would reject (bad namespaces, sizes past the limits)
		ValidationResult validate(const Limits& limits = {}) const;
		// One container holding every entry (empty when there are none)
		std::vector<std::uint8_t> serialize() const;

	private:
		std::vector<CustomEntry> entries_;
	};

	// What a .memidx section held: the entries of every readable container, in order, and the problems found. Problems
	// are about custom metadata only; they never make the identity descriptor unreadable.
	struct CustomParse
	{
		CustomMetadata metadata;
		std::size_t containers = 0;
		std::vector<Issue> issues;			// Errors: something was skipped. Warnings: read, but unusual.

		bool ok() const
		{
			for (const Issue& i : issues)
				if (i.severity == Issue::Severity::error) return false;
			return true;
		}
	};

	// Every container in a .memidx section (zero padding between them is skipped). An entry with a bad namespace is
	// skipped and reported; a container whose sizes don't add up stops the parse there (earlier entries are kept).
	MEMID_API CustomParse parse_custom_section(std::span<const std::uint8_t> section, const Limits& limits = {});

	// ---- Compile-time containers (memid/embed.hpp) --------------------------------------------------------------------
	namespace detail
	{
		// One container with one entry, built at compile time. NS and P are the sizes without a terminating NUL.
		template <std::size_t NS, std::size_t P>
		constexpr std::array<std::uint8_t, kCustomHeaderSize + kCustomEntryHeaderSize + NS + P>
		custom_container(const char (&ns)[NS + 1], std::uint16_t format, const std::uint8_t* payload)
		{
			static_assert(NS > 0 && NS <= 128, "a custom metadata namespace is 1-128 bytes");
			constexpr std::size_t total = kCustomHeaderSize + kCustomEntryHeaderSize + NS + P;
			static_assert(total <= 0xFFFFFFFFu, "custom metadata too large");
			std::array<std::uint8_t, total> b{};
			for (std::size_t i = 0; i < kCustomMagic.size(); ++i) b[i] = kCustomMagic[i];
			put16(&b[8], kCustomVersion);
			put16(&b[10], static_cast<std::uint16_t>(kCustomHeaderSize));
			put32(&b[12], static_cast<std::uint32_t>(total));
			put32(&b[16], 1);
			std::uint8_t* e = &b[kCustomHeaderSize];
			put16(e, static_cast<std::uint16_t>(NS));
			put16(e + 2, format);
			put32(e + 4, static_cast<std::uint32_t>(P));
			for (std::size_t i = 0; i < NS; ++i) e[kCustomEntryHeaderSize + i] = static_cast<std::uint8_t>(ns[i]);
			for (std::size_t i = 0; i < P; ++i) e[kCustomEntryHeaderSize + NS + i] = payload[i];
			return b;
		}

		// The same for a text payload given as a string literal (its terminating NUL is not stored)
		template <std::size_t NS, std::size_t T>
		constexpr auto custom_text_container(const char (&ns)[NS], std::uint16_t format, const char (&text)[T])
		{
			std::array<std::uint8_t, T - 1> bytes{};
			for (std::size_t i = 0; i + 1 < T; ++i) bytes[i] = static_cast<std::uint8_t>(text[i]);
			return custom_container<NS - 1, T - 1>(ns, format, bytes.data());
		}

		// The same for a byte array
		template <std::size_t NS, std::size_t P>
		constexpr auto custom_bytes_container(const char (&ns)[NS], std::uint16_t format, const unsigned char (&bytes)[P])
		{
			std::array<std::uint8_t, P> copy{};
			for (std::size_t i = 0; i < P; ++i) copy[i] = bytes[i];
			return custom_container<NS - 1, P>(ns, format, copy.data());
		}
	}
}
