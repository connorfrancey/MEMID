/* MEMID's C ABI: read and check MEMID data from any language that can call C (C, C#/P-Invoke, Python/ctypes, Rust ...).
 *
 * Stable rules: functions are never removed or changed, only added; structures that may grow start with `struct_size`
 * (set it to sizeof before the call: the library fills only what both sides know). No C++ types and no exceptions cross
 * this boundary. Strings are UTF-8. Memory the library allocates is freed only by the library (memid_custom_free).
 *
 * The C ABI is a convenience: the on-disk format (docs/MEMID_FORMAT.md) can be read without it.
 */
#ifndef MEMID_C_API_H
#define MEMID_C_API_H

#include <memid/export.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEMID_C_ABI_VERSION 1u

typedef enum memid_status
{
	MEMID_OK = 0,
	MEMID_ERROR_INVALID_ARGUMENT = 1,	/* A null pointer, or struct_size too small */
	MEMID_ERROR_TRUNCATED = 2,
	MEMID_ERROR_BAD_MAGIC = 3,
	MEMID_ERROR_UNSUPPORTED_VERSION = 4,
	MEMID_ERROR_MALFORMED = 5,
	MEMID_ERROR_LIMIT_EXCEEDED = 6,
	MEMID_ERROR_INVALID_FIELD = 7,
	MEMID_ERROR_UNKNOWN_CRITICAL = 8,
	MEMID_ERROR_NOT_FOUND = 9,			/* No .memid / .memidx section, or no descriptor in it */
	MEMID_ERROR_IO = 10,
	MEMID_ERROR_UNSUPPORTED_BINARY = 11,
	MEMID_ERROR_INVALID = 12,			/* Parsed, but validation found errors */
	MEMID_ERROR_INTERNAL = 13
} memid_status;

typedef struct memid_uuid { uint8_t bytes[16]; } memid_uuid;

/* A descriptor's fields. Text is NUL-terminated (the field sizes plus one). */
typedef struct memid_descriptor_info
{
	uint32_t struct_size;				/* In: sizeof(memid_descriptor_info) */
	uint16_t format_major, format_minor;
	uint32_t flags;
	memid_uuid mod_id, publisher_id, build_id;
	uint16_t version_major, version_minor, version_patch;
	uint16_t release_channel, architecture, operating_system;
	uint8_t binary_format, language;
	uint16_t runtime;
	uint64_t build_timestamp, toolchain_id;
	uint32_t game_id, game_version_min, game_version_max;
	uint32_t interop_min, interop_max;
	uint64_t capabilities, protocol_flags;
	uint32_t extension_count;
	char reverse_domain[97];
	char name[65];
	char publisher[49];
	char homepage[65];
} memid_descriptor_info;

/* The version of this C ABI (MEMID_C_ABI_VERSION of the library you load), and whether code written against
 * `abi_version` can use it (same or older version). */
MEMID_API uint32_t MEMID_CALL memid_abi_version(void);
MEMID_API int MEMID_CALL memid_abi_compatible(uint32_t abi_version);

/* The descriptor size and format version this library writes (428, 1.1) */
MEMID_API uint32_t MEMID_CALL memid_descriptor_size(void);
MEMID_API void MEMID_CALL memid_format_version(uint16_t* major, uint16_t* minor);

/* A descriptor in memory (from a .memid section, or memid_identity_descriptor): parsed into `out`. MEMID_ERROR_INVALID
 * when it parses but fails validation (`out` is filled anyway). */
MEMID_API memid_status MEMID_CALL memid_parse_descriptor(const uint8_t* data, size_t size, memid_descriptor_info* out);
MEMID_API memid_status MEMID_CALL memid_validate_descriptor(const uint8_t* data, size_t size);

/* The identity of a binary on disk, read without loading it. `path` is UTF-8. */
MEMID_API memid_status MEMID_CALL memid_read_file(const char* path, memid_descriptor_info* out);

/* "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" and a NUL into `out` (37 bytes) */
MEMID_API memid_status MEMID_CALL memid_uuid_to_string(const memid_uuid* uuid, char out[37]);

/* A human-readable text for a status (static storage, never NULL) */
MEMID_API const char* MEMID_CALL memid_status_text(memid_status status);

/* ---- Custom metadata (.memidx) ------------------------------------------------------------------------------------ */
typedef struct memid_custom memid_custom;	/* Opaque; owns copies of the entries */

typedef struct memid_custom_entry
{
	const char* namespace_id;			/* Not NUL-terminated: namespace_size bytes */
	uint32_t namespace_size;
	uint16_t format, flags;
	const uint8_t* payload;
	uint32_t payload_size;
} memid_custom_entry;					/* Points into the memid_custom it came from: valid until memid_custom_free */

/* A .memidx section in memory, or a binary's .memidx read without loading it. The result is freed with
 * memid_custom_free. MEMID_OK even when some entries were skipped (memid_custom_problem_count). */
MEMID_API memid_status MEMID_CALL memid_custom_parse(const uint8_t* section, size_t size, memid_custom** out);
MEMID_API memid_status MEMID_CALL memid_custom_read_file(const char* path, memid_custom** out);
MEMID_API uint32_t MEMID_CALL memid_custom_count(const memid_custom* custom);
MEMID_API uint32_t MEMID_CALL memid_custom_problem_count(const memid_custom* custom);
MEMID_API memid_status MEMID_CALL memid_custom_entry_at(const memid_custom* custom, uint32_t index, memid_custom_entry* out);
/* The first entry of a namespace (NUL-terminated); MEMID_ERROR_NOT_FOUND if none */
MEMID_API memid_status MEMID_CALL memid_custom_find(const memid_custom* custom, const char* namespace_id, memid_custom_entry* out);
MEMID_API void MEMID_CALL memid_custom_free(memid_custom* custom);

/* ---- In a binary that embeds its identity (memid/embed.hpp) --------------------------------------------------------
 * Such a binary exports:
 *     const unsigned char* memid_identity_descriptor(uint32_t* size);
 * returning its own 428 descriptor bytes (and the size). It is defined by MEMID_DEFINE_IDENTITY, not by this library. */

#ifdef __cplusplus
}
#endif

#endif
