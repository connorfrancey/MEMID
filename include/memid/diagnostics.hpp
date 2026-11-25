// What parsing and validation report: an error code that says what kind of problem it is, and text for people.
#pragma once

#include <memid/export.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace memid
{
	enum class ErrorCode
	{
		none = 0,
		truncated,				// Fewer bytes than the structure needs
		bad_magic,				// Not a MEMID descriptor / container
		unsupported_version,	// A major format version this reader doesn't implement
		malformed,				// Inconsistent sizes, offsets or counts
		limit_exceeded,			// Larger than the reader's Limits allow
		invalid_field,			// A field with a value the format forbids
		unknown_critical,		// A critical extension this reader doesn't know
		not_found,				// No descriptor / container where one was looked for
		io,						// The file couldn't be read
		unsupported_binary,		// A binary format this reader doesn't parse
	};

	MEMID_API std::string_view to_string(ErrorCode code) noexcept;

	struct Issue
	{
		enum class Severity { warning, error } severity = Severity::error;
		ErrorCode code = ErrorCode::none;
		std::string message;
	};

	// The outcome of validate(): errors make a descriptor unusable, warnings don't
	struct ValidationResult
	{
		std::vector<Issue> issues;

		bool ok() const
		{
			for (const Issue& i : issues)
				if (i.severity == Issue::Severity::error) return false;
			return true;
		}
	};
}
