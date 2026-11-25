/* The C ABI, compiled as C and linked against the shared library. Usage: memid_c_api_test MOD_DLL LEGACY_DLL */

#include <memid/c_api.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                                    \
	do {                                                                               \
		if (!(cond)) {                                                                 \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
			++failures;                                                                \
		}                                                                              \
	} while (0)

int main(int argc, char** argv)
{
	memid_descriptor_info info;
	memid_custom* custom = NULL;
	memid_custom_entry entry;
	uint16_t major = 0, minor = 0;
	char uuid[37];

	if (argc != 3) {
		fprintf(stderr, "usage: memid_c_api_test MOD_DLL LEGACY_DLL\n");
		return 2;
	}

	CHECK(memid_abi_version() == MEMID_C_ABI_VERSION);
	CHECK(memid_abi_compatible(1));
	CHECK(!memid_abi_compatible(MEMID_C_ABI_VERSION + 1));
	CHECK(memid_descriptor_size() == 428);
	memid_format_version(&major, &minor);
	CHECK(major == 1 && minor == 1);
	CHECK(strcmp(memid_status_text(MEMID_ERROR_NOT_FOUND), "not found") == 0);

	/* The identity of a binary on disk */
	memset(&info, 0, sizeof info);
	CHECK(memid_read_file(argv[1], &info) == MEMID_ERROR_INVALID_ARGUMENT);	/* struct_size not set */
	info.struct_size = sizeof info;
	CHECK(memid_read_file(argv[1], &info) == MEMID_OK);
	CHECK(strcmp(info.name, "Minimal Mod") == 0);
	CHECK(strcmp(info.reverse_domain, "com.example.minimal-mod") == 0);
	CHECK(info.version_major == 1 && info.version_minor == 2 && info.version_patch == 3);
	CHECK(memid_uuid_to_string(&info.mod_id, uuid) == MEMID_OK);
	CHECK(strcmp(uuid, "6f9619ff-8b86-d011-b42d-00c04fc964ff") == 0);

	/* A legacy binary, a missing file, bad arguments */
	CHECK(memid_read_file(argv[2], &info) == MEMID_ERROR_NOT_FOUND);
	CHECK(memid_read_file("no/such/file.dll", &info) == MEMID_ERROR_IO);
	CHECK(memid_parse_descriptor(NULL, 0, &info) == MEMID_ERROR_INVALID_ARGUMENT);
	{
		static const uint8_t junk[16] = {'X'};
		CHECK(memid_parse_descriptor(junk, sizeof junk, &info) == MEMID_ERROR_BAD_MAGIC);
		CHECK(memid_validate_descriptor(junk, 4) == MEMID_ERROR_TRUNCATED);
	}

	/* Custom metadata */
	CHECK(memid_custom_read_file(argv[1], &custom) == MEMID_OK);
	CHECK(memid_custom_count(custom) == 3);
	CHECK(memid_custom_problem_count(custom) == 0);
	CHECK(memid_custom_find(custom, "com.example.minimal-mod", &entry) == MEMID_OK);
	CHECK(entry.payload_size == strlen("Hello from the minimal mod"));
	CHECK(memcmp(entry.payload, "Hello from the minimal mod", entry.payload_size) == 0);
	CHECK(memid_custom_entry_at(custom, 99, &entry) == MEMID_ERROR_NOT_FOUND);
	CHECK(memid_custom_find(custom, "com.unknown.ns", &entry) == MEMID_ERROR_NOT_FOUND);
	memid_custom_free(custom);
	CHECK(memid_custom_read_file(argv[2], &custom) == MEMID_ERROR_NOT_FOUND);
	CHECK(custom == NULL);

	if (failures) fprintf(stderr, "%d check(s) failed\n", failures);
	else printf("C ABI: all checks passed\n");
	return failures ? 1 : 0;
}
