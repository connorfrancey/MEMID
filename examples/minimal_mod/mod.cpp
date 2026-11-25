// A minimal mod DLL that identifies itself with MEMID: its descriptor and two custom metadata entries are built at
// compile time and placed in the .memid and .memidx sections. A third custom entry (metadata.json) is added by
// memid_add_custom_metadata in CMakeLists.txt.

#include <memid/embed.hpp>

MEMID_DEFINE_IDENTITY(
	.mod_id = "6f9619ff-8b86-d011-b42d-00c04fc964ff",
	.reverse_domain = "com.example.minimal-mod",
	.name = "Minimal Mod",
	.publisher = "Example Publisher",
	.version = "1.2.3",
	.homepage = "https://example.com/minimal-mod",
	.publisher_id = "a3bb189e-8bf9-3888-9912-ace4e6543002",
	.runtime = memid::Runtime::mirage,
	.game_version_min = memid::pack_game_version(1, 0, 0),
	.interop_min = 1,
	.capabilities = memid::capability::gameplay | memid::capability::native_hooks,
	.protocol_flags = memid::protocol::mirage_api);

// A text note, and a few raw bytes under a second namespace
MEMID_DEFINE_CUSTOM_TEXT(readme, "com.example.minimal-mod", memid::MetadataFormat::text, "Hello from the minimal mod");

namespace
{
	constexpr unsigned char kBlob[] = {0x00, 0x01, 0x02, 0xFE, 0xFF};
}
MEMID_DEFINE_CUSTOM_BYTES(blob, "com.example.minimal-mod.blob", memid::MetadataFormat::raw, kBlob);

#if defined(_WIN32)
#  define EXAMPLE_EXPORT __declspec(dllexport)
#else
#  define EXAMPLE_EXPORT __attribute__((visibility("default")))
#endif

// The mod's own code
extern "C" EXAMPLE_EXPORT int example_mod_answer() { return 42; }
