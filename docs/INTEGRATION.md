# Integration

## CMake targets

| Target | What |
|---|---|
| `memid::memid` | the library: the static one when built, else the shared one |
| `memid::memid_static` | static library (`MEMID_STATIC` defined for users) |
| `memid::memid_shared` | shared library (`memid.dll` / `libmemid.so`), exports the C ABI and the C++ API |
| `memid::headers` | headers only: enough for `memid/embed.hpp` |
| `memid::memid_tools` | `memid-inspect` |

Static and shared libraries are compiled separately because on Windows the shared one's objects need `dllexport`.

## Options

| Option | Standalone | As a subproject |
|---|---|---|
| `MEMID_BUILD_STATIC` | ON | ON |
| `MEMID_BUILD_SHARED` | ON | OFF |
| `MEMID_BUILD_TOOLS` | ON | OFF |
| `MEMID_BUILD_TESTS` | ON | OFF |
| `MEMID_BUILD_EXAMPLES` | ON | OFF |
| `MEMID_ENABLE_INSTALL` | ON | OFF |

No global compiler flags, definitions or include directories are set. The tests fetch GoogleTest through CPM; the
library has no dependencies.

```cmake
find_package(MEMID 1.1 CONFIG REQUIRED)   # an install (cmake --install); brings memid_embed_identity too
add_subdirectory(path/to/MEMID)           # a source tree
```

Both are verified by `examples/consumers/`.

## In MIRAGE

MIRAGE (the Mirror's Edge mod loader) adds MEMID's source with CPM (`cmake/Libraries.cmake`, `MIRAGE_MEMID_SOURCE`, the
static library only) and reads native mods' identities in `loader/src/runtime/mod_identity.{h,cpp}`:

1. Before a native mod's DLL is loaded (`LoadNative`), `memid::inspect_file` reads its `.memid` and `.memidx` sections.
   Nothing in the DLL runs.
2. Policy:

   | The DLL | MIRAGE |
   |---|---|
   | has no `.memid` | loads it as a legacy mod (logged) |
   | has an unreadable or invalid descriptor | loads it as a legacy mod, with a warning; the identity isn't used |
   | is built for another architecture or OS, or another game | refuses to load it, saying why |
   | declares MIRAGE's ABI but not this runtime's version | refuses |
   | has a mod UUID another loaded mod already has | refuses |
   | passes | registers the identity (`memid::Registry`, module = the Mirage mod id) |

3. Unloading the mod (or a failed load, or a reload) forgets its identity, so no stale entry remains.
4. Other MIRAGE code queries `mirage::mods::identity::Find(modId)`, `FindByUuid(uuid)` and `All()`, which return copies.

MEMID is the embedded counterpart of a mod's `me-mod.json` (`reverse_domain` = its `id`). `mod.toml` keeps owning
entry points, settings and dependencies; MEMID carries only the binary's declared identity. Lua, C# script and content
mods have no binary of their own and so no MEMID identity.
