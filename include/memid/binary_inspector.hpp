// Finding MEMID data in a compiled binary without loading or running it: the file's section table is read, then only
// the .memid and .memidx sections. A PE without one of them is looked at once more for an RT_RCDATA resource of the
// same name (MEMID, MEMIDX: for binaries that can't get a section of their own, like .NET apps). PE (Windows EXE/DLL)
// and little-endian ELF are parsed; Mach-O is recognized and reported as unsupported. Every offset and size read from
// the file is bounds-checked.
//
//   const memid::Inspection i = memid::inspect_file("MyMod.dll");
//   if (i.descriptor) ...                        // the identity, parsed (validated: i.validation)
//   for (const auto& e : i.custom.metadata.entries()) ...
#pragma once

#include <memid/custom_metadata.hpp>
#include <memid/descriptor.hpp>
#include <memid/export.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace memid
{
	struct SectionInfo
	{
		std::string name;
		std::uint64_t file_offset = 0;	// Where its bytes are in the file
		std::uint64_t size = 0;			// How many of them hold data (PE: the smaller of the virtual and raw sizes)
	};

	// Where a binary's MEMID data was found
	enum class Location : std::uint8_t
	{
		none,		// Not found
		section,	// Its section (.memid / .memidx)
		resource,	// A PE RT_RCDATA resource named MEMID / MEMIDX (the binary has no section of that name)
	};

	struct Inspection
	{
		BinaryFormat format = BinaryFormat::unknown;
		Architecture architecture = Architecture::unknown;	// From the file's header
		std::vector<SectionInfo> sections;

		// The file itself: io, unsupported_binary or malformed (sections unreadable). none: the section table was read.
		ErrorCode file_error = ErrorCode::none;
		std::string file_message;

		// The standardized identity (.memid). not_found: the binary has none (a legacy binary).
		bool has_identity_section = false;	// Found, in its section or a resource (identity_location says which)
		Location identity_location = Location::none;
		std::optional<Descriptor> descriptor;
		ErrorCode identity_error = ErrorCode::not_found;
		std::string identity_message;
		std::vector<Issue> identity_warnings;
		ValidationResult validation;		// validate(*descriptor), when there is one
		std::vector<std::uint8_t> identity_bytes;	// The descriptor and its extensions as stored

		// Developer metadata (.memidx), reported apart: its problems never affect the identity above
		bool has_custom_section = false;	// Found, in its section or a resource (custom_location says which)
		Location custom_location = Location::none;
		CustomParse custom;

		// A descriptor that parsed and validated
		bool identified() const { return descriptor.has_value() && validation.ok(); }
	};

	// A binary in memory (the whole file)
	MEMID_API Inspection inspect_bytes(std::span<const std::uint8_t> file, const Limits& limits = {});
	// A binary on disk: reads its headers and the two sections, not the whole file
	MEMID_API Inspection inspect_file(const std::filesystem::path& path, const Limits& limits = {});

	// Where the descriptor starts in a .memid section's bytes: the first magic, skipping linker padding (zero bytes).
	// npos-like SIZE_MAX when there is none; `second` is set when another descriptor follows (an error: one identity per
	// binary).
	MEMID_API std::size_t find_descriptor(std::span<const std::uint8_t> section, bool* second = nullptr) noexcept;
}
