// A mod DLL with no MEMID code at all: its identity and a custom note are added by memid_embed_identity and
// memid_add_custom_metadata in CMakeLists.txt.

#if defined(_WIN32)
#  define EXAMPLE_EXPORT __declspec(dllexport)
#else
#  define EXAMPLE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" EXAMPLE_EXPORT int helper_mod_version() { return 234; }
