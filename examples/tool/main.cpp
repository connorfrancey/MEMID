// An executable that embeds its identity and reads it back through the getter MEMID_DEFINE_IDENTITY exports. Run with
// no arguments it prints its own identity; with a path, the identity of that binary (read without loading it).

#include <memid/embed.hpp>
#include <memid/memid.hpp>

#include <cstdio>
#include <string>

MEMID_DEFINE_IDENTITY(
	.mod_id = "7c9e6679-7425-40de-944b-e07fc1f90ae7",
	.reverse_domain = "com.example.memid-tool",
	.name = "MEMID Example Tool",
	.publisher = "Example Publisher",
	.version = "0.4.0",
	.channel = memid::ReleaseChannel::development,
	.game_id = memid::game::none,
	.capabilities = memid::capability::tooling);

namespace
{
	int Print(const memid::Descriptor& d)
	{
		std::printf("%s %s (%s), %s\n", d.name.c_str(), d.version.to_string().c_str(), d.reverse_domain.c_str(), d.mod_id.to_string().c_str());
		return 0;
	}
}

int main(int argc, char** argv)
{
	if (argc > 1) {
		const std::string path = argv[1];
		const memid::Inspection i = memid::inspect_file(std::filesystem::path(std::u8string(path.begin(), path.end())));
		if (!i.descriptor) {
			std::printf("%s: no identity (%s)\n", argv[1], i.identity_message.c_str());
			return 3;
		}
		return Print(*i.descriptor);
	}
	std::uint32_t size = 0;
	const unsigned char* bytes = memid_identity_descriptor(&size);
	const memid::ParseResult r = memid::parse({bytes, size});
	if (!r) {
		std::printf("own identity unreadable: %s\n", r.message.c_str());
		return 4;
	}
	return Print(*r.descriptor);
}
