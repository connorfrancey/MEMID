// A consumer of MEMID (installed package or subproject): its own identity comes from memid_embed_identity in CMake;
// it reads it back from its own file with the inspector.

#include <memid/memid.hpp>

#include <cstdio>
#include <string>

int main(int, char** argv)
{
	const std::string self = argv[0];
	const memid::Inspection i = memid::inspect_file(std::filesystem::path(std::u8string(self.begin(), self.end())));
	if (!i.descriptor) {
		std::printf("no identity: %s\n", i.identity_message.c_str());
		return 1;
	}
	std::printf("%s %s, valid: %s\n", i.descriptor->name.c_str(), i.descriptor->version.to_string().c_str(), i.validation.ok() ? "yes" : "no");
	return i.validation.ok() ? 0 : 1;
}
