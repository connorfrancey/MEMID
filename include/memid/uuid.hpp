// A 128-bit UUID as MEMID stores it: the 16 bytes in the order they are written in text (RFC 9562's network order),
// so "00112233-4455-6677-8899-aabbccddeeff" is the bytes 00 11 22 ... ff. Parsing and formatting are constexpr, so
// embedded descriptors are built at compile time.
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace memid
{
	struct Uuid
	{
		std::array<std::uint8_t, 16> bytes{};

		constexpr bool nil() const
		{
			for (std::uint8_t b : bytes)
				if (b) return false;
			return true;
		}
		friend constexpr bool operator==(const Uuid&, const Uuid&) = default;
		friend constexpr auto operator<=>(const Uuid&, const Uuid&) = default;

		// "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", hex digits in either case; braces are not accepted
		static constexpr std::optional<Uuid> parse(std::string_view text)
		{
			if (text.size() != 36) return std::nullopt;
			Uuid u;
			std::size_t at = 0;
			for (std::size_t i = 0; i < 36;) {
				if (i == 8 || i == 13 || i == 18 || i == 23) {
					if (text[i] != '-') return std::nullopt;
					++i;
					continue;
				}
				const int hi = Hex(text[i]), lo = Hex(text[i + 1]);
				if (hi < 0 || lo < 0) return std::nullopt;
				u.bytes[at++] = static_cast<std::uint8_t>(hi << 4 | lo);
				i += 2;
			}
			return u;
		}

		// Lowercase, with dashes
		std::string to_string() const
		{
			static constexpr char kHex[] = "0123456789abcdef";
			std::string s;
			s.reserve(36);
			for (std::size_t i = 0; i < 16; ++i) {
				if (i == 4 || i == 6 || i == 8 || i == 10) s.push_back('-');
				s.push_back(kHex[bytes[i] >> 4]);
				s.push_back(kHex[bytes[i] & 0xF]);
			}
			return s;
		}

	private:
		static constexpr int Hex(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}
	};

	struct UuidHash
	{
		std::size_t operator()(const Uuid& u) const noexcept
		{
			// FNV-1a over the bytes: UUIDs are already well distributed, this only folds them to size_t
			std::uint64_t h = 1469598103934665603ull;
			for (std::uint8_t b : u.bytes) h = (h ^ b) * 1099511628211ull;
			return static_cast<std::size_t>(h);
		}
	};
}
