/* Reading a binary's MEMID identity and custom metadata from C, through the C ABI. Usage: read_identity FILE */

#include <memid/c_api.h>

#include <stdio.h>

int main(int argc, char** argv)
{
	memid_descriptor_info info = {0};
	memid_custom* custom = NULL;
	memid_status status;
	uint32_t i;
	char uuid[37];

	if (argc != 2) {
		fprintf(stderr, "usage: read_identity FILE\n");
		return 1;
	}
	if (!memid_abi_compatible(MEMID_C_ABI_VERSION)) {
		fprintf(stderr, "this MEMID library (C ABI %u) is older than this program\n", memid_abi_version());
		return 1;
	}

	info.struct_size = sizeof info;
	status = memid_read_file(argv[1], &info);
	if (status != MEMID_OK && status != MEMID_ERROR_INVALID) {
		printf("%s: no identity (%s)\n", argv[1], memid_status_text(status));
		return 3;
	}
	memid_uuid_to_string(&info.mod_id, uuid);
	printf("%s %u.%u.%u\n  id: %s\n  mod uuid: %s\n  publisher: %s\n  valid: %s\n", info.name, info.version_major,
		   info.version_minor, info.version_patch, info.reverse_domain, uuid, info.publisher, status == MEMID_OK ? "yes" : "no");

	if (memid_custom_read_file(argv[1], &custom) == MEMID_OK) {
		printf("  custom metadata: %u entries\n", memid_custom_count(custom));
		for (i = 0; i < memid_custom_count(custom); ++i) {
			memid_custom_entry e;
			if (memid_custom_entry_at(custom, i, &e) == MEMID_OK)
				printf("    %.*s: format %u, %u bytes\n", (int)e.namespace_size, e.namespace_id, e.format, e.payload_size);
		}
		memid_custom_free(custom);
	}
	return 0;
}
