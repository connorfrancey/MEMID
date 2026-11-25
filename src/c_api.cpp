// The C ABI over the C++ library. Nothing thrown inside escapes: every entry point catches and returns a status.

#include <memid/binary_inspector.hpp>
#include <memid/c_api.h>
#include <memid/descriptor.hpp>

#include <algorithm>
#include <cstring>
#include <new>

struct memid_custom
{
	memid::CustomParse parse;
};

namespace
{
	memid_status Status(memid::ErrorCode code)
	{
		using memid::ErrorCode;
		switch (code) {
		case ErrorCode::none: return MEMID_OK;
		case ErrorCode::truncated: return MEMID_ERROR_TRUNCATED;
		case ErrorCode::bad_magic: return MEMID_ERROR_BAD_MAGIC;
		case ErrorCode::unsupported_version: return MEMID_ERROR_UNSUPPORTED_VERSION;
		case ErrorCode::malformed: return MEMID_ERROR_MALFORMED;
		case ErrorCode::limit_exceeded: return MEMID_ERROR_LIMIT_EXCEEDED;
		case ErrorCode::invalid_field: return MEMID_ERROR_INVALID_FIELD;
		case ErrorCode::unknown_critical: return MEMID_ERROR_UNKNOWN_CRITICAL;
		case ErrorCode::not_found: return MEMID_ERROR_NOT_FOUND;
		case ErrorCode::io: return MEMID_ERROR_IO;
		case ErrorCode::unsupported_binary: return MEMID_ERROR_UNSUPPORTED_BINARY;
		}
		return MEMID_ERROR_INTERNAL;
	}

	template <std::size_t N>
	void CopyText(char (&out)[N], const std::string& s)
	{
		const std::size_t n = std::min(s.size(), N - 1);
		std::memcpy(out, s.data(), n);
		std::memset(out + n, 0, N - n);
	}

	void CopyUuid(memid_uuid& out, const memid::Uuid& u) { std::memcpy(out.bytes, u.bytes.data(), 16); }

	// Fills `out` up to the size the caller declared (older callers get the fields they know)
	void Fill(const memid::Descriptor& d, memid_descriptor_info* out)
	{
		memid_descriptor_info full{};
		full.struct_size = out->struct_size;
		full.format_major = d.format_major;
		full.format_minor = d.format_minor;
		full.flags = d.flags;
		CopyUuid(full.mod_id, d.mod_id);
		CopyUuid(full.publisher_id, d.publisher_id);
		CopyUuid(full.build_id, d.build_id);
		full.version_major = d.version.major;
		full.version_minor = d.version.minor;
		full.version_patch = d.version.patch;
		full.release_channel = static_cast<uint16_t>(d.channel);
		full.architecture = static_cast<uint16_t>(d.architecture);
		full.operating_system = static_cast<uint16_t>(d.os);
		full.binary_format = static_cast<uint8_t>(d.binary_format);
		full.language = static_cast<uint8_t>(d.language);
		full.runtime = static_cast<uint16_t>(d.runtime);
		full.build_timestamp = d.build_timestamp;
		full.toolchain_id = d.toolchain_id;
		full.game_id = d.game_id;
		full.game_version_min = d.game_version_min;
		full.game_version_max = d.game_version_max;
		full.interop_min = d.interop_min;
		full.interop_max = d.interop_max;
		full.capabilities = d.capabilities;
		full.protocol_flags = d.protocol_flags;
		full.extension_count = static_cast<uint32_t>(d.extensions.size());
		CopyText(full.reverse_domain, d.reverse_domain);
		CopyText(full.name, d.name);
		CopyText(full.publisher, d.publisher);
		CopyText(full.homepage, d.homepage);
		std::memcpy(out, &full, std::min<std::size_t>(out->struct_size, sizeof full));
	}

	bool InfoOk(const memid_descriptor_info* out)
	{
		return out && out->struct_size >= offsetof(memid_descriptor_info, extension_count);	// Version 1's fields up to the counts
	}

	memid::CustomEntry const* EntryAt(const memid_custom* c, uint32_t index)
	{
		const auto& entries = c->parse.metadata.entries();
		return index < entries.size() ? &entries[index] : nullptr;
	}

	void FillEntry(const memid::CustomEntry& e, memid_custom_entry* out)
	{
		out->namespace_id = e.namespace_id.data();
		out->namespace_size = static_cast<uint32_t>(e.namespace_id.size());
		out->format = e.format;
		out->flags = e.flags;
		out->payload = e.payload.data();
		out->payload_size = static_cast<uint32_t>(e.payload.size());
	}

	std::filesystem::path Utf8Path(const char* path)
	{
		const std::string s(path);
		return std::filesystem::path(std::u8string(s.begin(), s.end()));
	}
}

extern "C" {

uint32_t MEMID_CALL memid_abi_version(void) { return MEMID_C_ABI_VERSION; }
int MEMID_CALL memid_abi_compatible(uint32_t abi_version) { return abi_version >= 1 && abi_version <= MEMID_C_ABI_VERSION; }
uint32_t MEMID_CALL memid_descriptor_size(void) { return memid::kDescriptorSize; }

void MEMID_CALL memid_format_version(uint16_t* major, uint16_t* minor)
{
	if (major) *major = memid::kFormatMajor;
	if (minor) *minor = memid::kFormatMinor;
}

memid_status MEMID_CALL memid_parse_descriptor(const uint8_t* data, size_t size, memid_descriptor_info* out)
{
	if (!data || !InfoOk(out)) return MEMID_ERROR_INVALID_ARGUMENT;
	try {
		const memid::ParseResult r = memid::parse({data, size});
		if (!r) return Status(r.error);
		Fill(*r.descriptor, out);
		return memid::validate(*r.descriptor).ok() ? MEMID_OK : MEMID_ERROR_INVALID;
	} catch (const std::bad_alloc&) {
		return MEMID_ERROR_LIMIT_EXCEEDED;
	} catch (...) {
		return MEMID_ERROR_INTERNAL;
	}
}

memid_status MEMID_CALL memid_validate_descriptor(const uint8_t* data, size_t size)
{
	if (!data) return MEMID_ERROR_INVALID_ARGUMENT;
	try {
		const memid::ParseResult r = memid::parse({data, size});
		if (!r) return Status(r.error);
		return memid::validate(*r.descriptor).ok() ? MEMID_OK : MEMID_ERROR_INVALID;
	} catch (...) {
		return MEMID_ERROR_INTERNAL;
	}
}

memid_status MEMID_CALL memid_read_file(const char* path, memid_descriptor_info* out)
{
	if (!path || !InfoOk(out)) return MEMID_ERROR_INVALID_ARGUMENT;
	try {
		const memid::Inspection i = memid::inspect_file(Utf8Path(path));
		if (i.file_error != memid::ErrorCode::none) return Status(i.file_error);
		if (!i.descriptor) return Status(i.identity_error);
		Fill(*i.descriptor, out);
		return i.validation.ok() ? MEMID_OK : MEMID_ERROR_INVALID;
	} catch (...) {
		return MEMID_ERROR_INTERNAL;
	}
}

memid_status MEMID_CALL memid_uuid_to_string(const memid_uuid* uuid, char out[37])
{
	if (!uuid || !out) return MEMID_ERROR_INVALID_ARGUMENT;
	memid::Uuid u;
	std::memcpy(u.bytes.data(), uuid->bytes, 16);
	const std::string s = u.to_string();
	std::memcpy(out, s.c_str(), 37);
	return MEMID_OK;
}

const char* MEMID_CALL memid_status_text(memid_status status)
{
	switch (status) {
	case MEMID_OK: return "ok";
	case MEMID_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case MEMID_ERROR_TRUNCATED: return "truncated";
	case MEMID_ERROR_BAD_MAGIC: return "not MEMID data";
	case MEMID_ERROR_UNSUPPORTED_VERSION: return "unsupported format version";
	case MEMID_ERROR_MALFORMED: return "malformed";
	case MEMID_ERROR_LIMIT_EXCEEDED: return "limit exceeded";
	case MEMID_ERROR_INVALID_FIELD: return "invalid field";
	case MEMID_ERROR_UNKNOWN_CRITICAL: return "unknown critical extension";
	case MEMID_ERROR_NOT_FOUND: return "not found";
	case MEMID_ERROR_IO: return "I/O error";
	case MEMID_ERROR_UNSUPPORTED_BINARY: return "unsupported binary format";
	case MEMID_ERROR_INVALID: return "fails validation";
	case MEMID_ERROR_INTERNAL: return "internal error";
	}
	return "unknown status";
}

memid_status MEMID_CALL memid_custom_parse(const uint8_t* section, size_t size, memid_custom** out)
{
	if (!section || !out) return MEMID_ERROR_INVALID_ARGUMENT;
	*out = nullptr;
	try {
		auto* c = new memid_custom{memid::parse_custom_section({section, size})};
		*out = c;
		return MEMID_OK;
	} catch (...) {
		return MEMID_ERROR_INTERNAL;
	}
}

memid_status MEMID_CALL memid_custom_read_file(const char* path, memid_custom** out)
{
	if (!path || !out) return MEMID_ERROR_INVALID_ARGUMENT;
	*out = nullptr;
	try {
		memid::Inspection i = memid::inspect_file(Utf8Path(path));
		if (i.file_error != memid::ErrorCode::none) return Status(i.file_error);
		if (!i.has_custom_section) return MEMID_ERROR_NOT_FOUND;
		*out = new memid_custom{std::move(i.custom)};
		return MEMID_OK;
	} catch (...) {
		return MEMID_ERROR_INTERNAL;
	}
}

uint32_t MEMID_CALL memid_custom_count(const memid_custom* custom)
{
	return custom ? static_cast<uint32_t>(custom->parse.metadata.size()) : 0;
}

uint32_t MEMID_CALL memid_custom_problem_count(const memid_custom* custom)
{
	return custom ? static_cast<uint32_t>(custom->parse.issues.size()) : 0;
}

memid_status MEMID_CALL memid_custom_entry_at(const memid_custom* custom, uint32_t index, memid_custom_entry* out)
{
	if (!custom || !out) return MEMID_ERROR_INVALID_ARGUMENT;
	const memid::CustomEntry* e = EntryAt(custom, index);
	if (!e) return MEMID_ERROR_NOT_FOUND;
	FillEntry(*e, out);
	return MEMID_OK;
}

memid_status MEMID_CALL memid_custom_find(const memid_custom* custom, const char* namespace_id, memid_custom_entry* out)
{
	if (!custom || !namespace_id || !out) return MEMID_ERROR_INVALID_ARGUMENT;
	const memid::CustomEntry* e = custom->parse.metadata.find(namespace_id);
	if (!e) return MEMID_ERROR_NOT_FOUND;
	FillEntry(*e, out);
	return MEMID_OK;
}

void MEMID_CALL memid_custom_free(memid_custom* custom) { delete custom; }

}
