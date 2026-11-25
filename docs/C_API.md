# The C ABI

`include/memid/c_api.h`, exported by the shared library (`memid.dll`, `libmemid.so`). Plain C: no C++ types and no
exceptions cross it. It is a convenience — the format ([MEMID_FORMAT.md](MEMID_FORMAT.md)) can be read without it.

## Rules

- **Calling convention:** `__cdecl` on 32-bit Windows (stated explicitly), the platform default elsewhere.
- **Stability:** functions are only ever added (`MEMID_C_ABI_VERSION`, now 1). `memid_abi_compatible(v)` tells code
  written against version `v` whether this library serves it.
- **Growing structures** start with `struct_size`: set it to `sizeof` before the call; the library fills only what both
  sides know, and refuses a size smaller than version 1's.
- **Memory:** `memid_descriptor_info` is filled into your memory. A `memid_custom*` is allocated by the library and freed
  only with `memid_custom_free`; entry pointers point into it and are valid until then.
- **Strings** are UTF-8; text fields in `memid_descriptor_info` are NUL-terminated.
- **Errors** are `memid_status` values; `memid_status_text` names them. `MEMID_ERROR_INVALID` means "parsed but fails
  validation" (the info is filled anyway).

## Functions

| Function | |
|---|---|
| `memid_abi_version`, `memid_abi_compatible` | ABI version checks |
| `memid_descriptor_size`, `memid_format_version` | 428, 1.1 |
| `memid_parse_descriptor(data, size, info*)` | a descriptor in memory |
| `memid_validate_descriptor(data, size)` | parse and validate only |
| `memid_read_file(path, info*)` | a binary's identity, read without loading it |
| `memid_uuid_to_string(uuid*, char[37])` | |
| `memid_custom_parse`, `memid_custom_read_file` | custom metadata → `memid_custom*` |
| `memid_custom_count`, `memid_custom_problem_count`, `memid_custom_entry_at`, `memid_custom_find`, `memid_custom_free` | |

A binary that embeds its identity also exports `memid_identity_descriptor(uint32_t* size)` (from `MEMID_DEFINE_IDENTITY`,
not this library).

## C

```c
memid_descriptor_info info = {0};
info.struct_size = sizeof info;
if (memid_read_file("MyMod.dll", &info) == MEMID_OK) printf("%s\n", info.name);
```

Full example: `examples/c/read_identity.c`; ABI test: `tests/test_c_api.c`.

## C# (P/Invoke)

`examples/csharp/Program.cs` declares `memid_descriptor_info` as a sequential struct with `fixed` buffers and calls
`memid_read_file` (`CallingConvention.Cdecl`). MEMID UUIDs are in text order: `new Guid(bytes, bigEndian: true)`.

```sh
dotnet run --project examples/csharp -- path/to/memid.dll MyMod.dll
```

## Python (ctypes)

`examples/python/memid_read.py --library path/to/memid.dll MyMod.dll` uses the C ABI; `--direct` reads the PE section and
the descriptor in pure Python, without the library. ctypes can only load a library of the interpreter's bitness (a
64-bit Python can't load the x86 `memid.dll`); `--direct` has no such limit.

Verified: the C test and example, the C# example (.NET 11, x64) and both Python modes against the example mod.
