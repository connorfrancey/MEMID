// Standardized extensions: TLV records stored right after the descriptor in .memid, located by the descriptor's
// extension offset and size. Each record is type (u16), flags (u16), payload size (u32), then the payload, packed with
// no padding. Small structured additions to the standard live here; developer data belongs in .memidx
// (memid/custom_metadata.hpp).
//
// Unknown records are kept as they are (parse, then serialize, gives the same bytes); an unknown record flagged
// critical makes the descriptor unusable to this reader.
#pragma once

#include <memid/diagnostics.hpp>
#include <memid/export.hpp>
#include <memid/format.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace memid
{
	struct Extension
	{
		std::uint16_t type = 0;
		std::uint16_t flags = 0;
		std::vector<std::uint8_t> payload;

		bool critical() const { return (flags & extension_flag::critical) != 0; }
		friend bool operator==(const Extension&, const Extension&) = default;
	};

	// Whether this library knows what `type` means (the standard's reserved types)
	MEMID_API bool is_known_extension(std::uint16_t type) noexcept;

	struct ExtensionParse
	{
		std::vector<Extension> records;
		ErrorCode error = ErrorCode::none;	// none: every record was read
		std::string message;
		explicit operator bool() const { return error == ErrorCode::none; }
	};

	// The records in `data` (exactly the extension area). Stops at the first record that runs past the end.
	MEMID_API ExtensionParse parse_extensions(std::span<const std::uint8_t> data, const Limits& limits = {});

	// The records as bytes, in order
	MEMID_API std::vector<std::uint8_t> serialize_extensions(const std::vector<Extension>& records);

	// The first record of `type` (nullptr: none)
	MEMID_API const Extension* find_extension(const std::vector<Extension>& records, std::uint16_t type) noexcept;
}
