# Embedding an identity

## From source

```cpp
#include <memid/embed.hpp>

MEMID_DEFINE_IDENTITY(
    .mod_id = "6f9619ff-8b86-d011-b42d-00c04fc964ff",
    .reverse_domain = "com.example.wallrun-trails",
    .name = "Wallrun Trails",
    .publisher = "Example",
    .version = "1.2.0",
    .runtime = memid::Runtime::mirage,
    .interop_min = 1,
    .capabilities = memid::capability::gameplay);

MEMID_DEFINE_CUSTOM_TEXT(settings, "com.example.wallrun-trails", memid::MetadataFormat::json, R"({"trail":"blue"})");
```

The arguments are designated initializers of `memid::embed::Identity` (`include/memid/embed.hpp`), in its member
order. Defaults: architecture, OS, binary format and toolchain of the compiler; game Mirror's Edge; language C++;
channel stable; build timestamp 0 (so builds stay reproducible).

- The descriptor is computed at compile time (`constinit`): nothing runs at startup, nothing is allocated. An invalid
  UUID or version, or text too long for its field, **fails to compile**.
- `MEMID_DEFINE_IDENTITY` also defines the exported C function
  `const unsigned char* memid_identity_descriptor(uint32_t* size)`, so it can appear once per binary (a second use fails
  to link). `protocol::c_abi_getter` is always set.
- `MEMID_DEFINE_CUSTOM_TEXT(id, ns, format, "literal")` and `MEMID_DEFINE_CUSTOM_BYTES(id, ns, format, array)` add one
  custom container each; `id` is any identifier unique in the binary.
- Embedding needs only the headers (`memid::headers`), not the library.

## From CMake

```cmake
memid_embed_identity(TARGET MyMod
    MOD_ID 6f9619ff-8b86-d011-b42d-00c04fc964ff
    ID com.example.my-mod NAME "My Mod" VERSION 1.0.0
    [PUBLISHER text] [PUBLISHER_ID uuid] [HOMEPAGE url]
    [CHANNEL stable|beta|alpha|development|nightly] [RUNTIME none|mirage|dotnet|lua]
    [LANGUAGE c|cpp|csharp|rust|zig|d|go|pascal|other] [INTEROP_MIN n] [INTEROP_MAX n])

memid_add_custom_metadata(TARGET MyMod NAMESPACE com.example.my-mod FORMAT JSON FILE metadata.json)
```

Values are checked when CMake runs (UUID, identifier grammar, version, field lengths; JSON files must parse). Each
call writes one C++ source into `<build>/memid_generated/` — rewritten only when its content changes — and adds it to
the target. The metadata file is a configure dependency: editing it re-runs CMake. No binary is patched after linking.
FORMAT is RAW, TEXT, JSON, XML, CBOR, MSGPACK, TOML or a private number 32768–65535; files are limited to 1 MiB.

## How it is placed and kept

| Compiler | Placement | Kept in optimized builds by |
|---|---|---|
| MSVC, clang-cl | `#pragma section(".memid", read)`, `__declspec(allocate)` | the exported getter referencing the descriptor; `/INCLUDE:` for custom data |
| GCC, Clang (ELF) | `__attribute__((section(".memid"), used, retain))` | `used`/`retain`; the exported getter |

Verified: MSVC and clang-cl, PE DLLs and EXEs, x86 and x64, Debug and Release (tests/test_binaries.cpp inspects the
built examples; the getter's bytes match the section's). Not yet verified on a Linux toolchain. Mach-O placement is not
implemented.

The linker may pad between contributions to a section; readers skip zero bytes, which is why containers start with a
non-zero magic.
