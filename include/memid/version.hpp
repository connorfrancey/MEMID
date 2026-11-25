// A mod's version as MEMID stores it: major, minor and patch (16 bits each). Prerelease and build labels are not part
// of the binary descriptor; the release channel field says what kind of build it is.
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace memid
{
	struct Version
	{
		std::uint16_t major = 0, minor = 0, patch = 0;

		friend constexpr bool operator==(const Version&, const Version&) = default;
		friend constexpr auto operator<=>(const Version&, const Version&) = default;

		// "1.2.3": three decimal numbers 0-65535 without leading zeros (except "0"). Nothing else is accepted.
		static constexpr std::optional<Version> parse(std::string_view text)
		{
			Version v;
			std::uint16_t* parts[3] = {&v.major, &v.minor, &v.patch};
			std::size_t i = 0;
			for (int part = 0; part < 3; ++part) {
				if (part > 0) {
					if (i >= text.size() || text[i] != '.') return std::nullopt;
					++i;
				}
				const std::size_t start = i;
				std::uint32_t value = 0;
				while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
					value = value * 10 + static_cast<std::uint32_t>(text[i] - '0');
					if (value > 0xFFFF) return std::nullopt;
					++i;
				}
				if (i == start || (i - start > 1 && text[start] == '0')) return std::nullopt;
				*parts[part] = static_cast<std::uint16_t>(value);
			}
			if (i != text.size()) return std::nullopt;
			return v;
		}

		std::string to_string() const
		{
			return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
		}
	};
}
