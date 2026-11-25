# MEMID — Mirror's Edge Mod Identifier

MEMID is an identity format embedded in compiled binaries (EXE, DLL, shared libraries). A mod, tool or plugin carries
a small fixed descriptor — its permanent UUID, a reverse-domain id, name, publisher, version, target platform and game,
supported protocol versions — in a `.memid` section, and optional developer-defined metadata in a separate `.memidx`
section. Anything can read them from the file **without loading or running the binary**.

MEMID is its own library, independent of MIRAGE (the Mirror's Edge mod loader, which consumes it). It has no
third-party dependencies.

- [Format specification](docs/MEMID_FORMAT.md): the 428-byte v1.1 descriptor, extensions, the custom section
- [Embedding](docs/EMBEDDING.md): `MEMID_DEFINE_IDENTITY`, the CMake helpers, compilers and linkers
- [Custom metadata](docs/CUSTOM_METADATA.md): the `.memidx` container, namespaces, payload formats
- [C ABI](docs/C_API.md): C, C# (P/Invoke) and Python (ctypes)
- [Integration](docs/INTEGRATION.md): CMake targets and options, MIRAGE's use
- [ABI and format compatibility](docs/ABI_COMPATIBILITY.md) · [Security](docs/SECURITY.md)

## Supported platforms (verified)

| What | Verified |
|---|---|
| Building the library and tests | Windows: MSVC 19.5x (x86, x64; Debug, Release), clang-cl (x64) |
| Embedding (`.memid` / `.memidx` in the binary, kept in Release) | PE DLLs and EXEs built by MSVC and clang-cl |
| Reading binaries | PE (real files); ELF 32/64-bit little-endian (synthetic test files only) |
| Mach-O | Recognized and reported as unsupported |

ELF embedding (GCC/Clang `section` attributes) is implemented but has not been built and checked on Linux yet.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix <prefix>
```

## Use

```cmake
find_package(MEMID 1.1 CONFIG REQUIRED)        # or: add_subdirectory(path/to/MEMID)
target_link_libraries(MyTool PRIVATE memid::memid)

memid_embed_identity(TARGET MyMod
    MOD_ID 6f9619ff-8b86-d011-b42d-00c04fc964ff
    ID com.example.my-mod NAME "My Mod" VERSION 1.0.0 PUBLISHER "Example")
```

```cpp
#include <memid/memid.hpp>

const memid::Inspection i = memid::inspect_file("MyMod.dll");   // reads the file, never loads it
if (i.identified()) std::puts(i.descriptor->name.c_str());
```

```sh
memid-inspect --custom --decode MyMod.dll
memid-inspect --json MyMod.dll
```

A descriptor identifies a binary. It does **not** prove who built it or that it is safe ([Security](docs/SECURITY.md)).
