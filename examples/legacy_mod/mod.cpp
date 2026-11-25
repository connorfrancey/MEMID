// A DLL without MEMID data: memid-inspect and MIRAGE report it as having no identity (a legacy binary).

#if defined(_WIN32)
#  define EXAMPLE_EXPORT __declspec(dllexport)
#else
#  define EXAMPLE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" EXAMPLE_EXPORT int legacy_mod_answer() { return 7; }
