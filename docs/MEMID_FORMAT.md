# MEMID format 1.1

All integers are little-endian and unsigned. Structures are byte-packed: no padding anywhere. Text is UTF-8.
`schemas/memid-v1.1-layout.json` holds the same layout in machine-readable form; the tests check every offset
against it.

## Where the data is

| Binary | Identity | Custom metadata |
|---|---|---|
| PE (Windows EXE/DLL) | section `.memid` | section `.memidx` |
| ELF | section `.memid` | section `.memidx` |
| Mach-O | not defined yet | not defined yet |

A reader takes the section's bytes (PE: the smaller of the section's virtual and raw sizes), skips leading zero bytes
(linker padding), and expects the descriptor magic. One binary has one descriptor: a second magic after the first
descriptor is an error.

## The descriptor (428 bytes)

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 8 | magic | `4D 45 4D 49 44 0D 0A 1A` ("MEMID" CR LF SUB) |
| 8 | 2 | format_major | 1 |
| 10 | 2 | format_minor | 1 |
| 12 | 4 | descriptor_size | 428 for 1.1; a later 1.x may be larger |
| 16 | 4 | flags | bit 0 debug build, bit 1 has custom metadata (a hint); others reserved |
| 20 | 16 | mod_id | the mod's permanent UUID (required, not nil) |
| 36 | 16 | publisher_id | the publisher's UUID (nil: none) |
| 52 | 2 | version_major | |
| 54 | 2 | version_minor | |
| 56 | 2 | version_patch | |
| 58 | 2 | release_channel | 0 unspecified, 1 stable, 2 beta, 3 alpha, 4 development, 5 nightly |
| 60 | 2 | architecture | 0 unknown, 1 x86, 2 x86-64, 3 ARM, 4 ARM64, 0xFFFF any |
| 62 | 2 | operating_system | 0 unknown, 1 Windows, 2 Linux, 3 macOS, 0xFFFF any |
| 64 | 1 | binary_format | 0 unknown, 1 PE, 2 ELF, 3 Mach-O |
| 65 | 1 | language | 0 unknown, 1 C, 2 C++, 3 C#, 4 Rust, 5 Zig, 6 D, 7 Go, 8 Pascal, 255 other |
| 66 | 2 | runtime | 0 none, 1 MIRAGE, 2 .NET, 3 Lua, 0xFFFF other |
| 68 | 16 | build_id | one build of this version (nil: none) |
| 84 | 8 | build_timestamp | seconds since 1970-01-01 UTC (0: unknown) |
| 92 | 8 | toolchain_id | compiler family (16 bits: 1 MSVC, 2 Clang, 3 GCC, 4 clang-cl, 5 rustc, 6 .NET), then major, minor, patch (16 bits each) |
| 100 | 4 | game_id | 0 none, 1 Mirror's Edge (2008, PC) |
| 104 | 4 | game_version_min | packed `major << 24 \| minor << 16 \| patch`; 0: no limit |
| 108 | 4 | game_version_max | packed; 0: no limit |
| 112 | 4 | interop_min | lowest protocol version spoken (MIRAGE: its mod ABI); 0: no limit |
| 116 | 4 | interop_max | highest; 0: no limit |
| 120 | 8 | reserved | zero (a reader warns otherwise) |
| 128 | 8 | capabilities | declared functionality: bit 0 gameplay, 1 rendering, 2 UI, 3 audio, 4 input, 5 networking, 6 filesystem, 7 scripting, 8 content, 9 native hooks, 10 tooling |
| 136 | 8 | protocol_flags | bit 0 exports `memid_identity_descriptor`, bit 1 speaks MIRAGE's mod ABI |
| 144 | 96 | reverse_domain | e.g. `com.example.wallrun-trails` |
| 240 | 64 | name | display name |
| 304 | 48 | publisher | publisher display name |
| 352 | 64 | homepage | URL |
| 416 | 4 | extension_offset | from the start of the descriptor; 0 with size 0: none |
| 420 | 4 | extension_size | bytes |
| 424 | 4 | extension_flags | reserved, zero |

UUIDs are stored as their 16 bytes in text order (`00112233-...` is `00 11 22 33 ...`).

**Text fields** are padded with NUL bytes. A value may fill its whole field (no terminator); every byte after the
first NUL must be NUL. A reader rejects text that isn't UTF-8.

**Versioning.** A reader accepts any 1.x whose `descriptor_size` is at least 428 and ignores fields past the ones it
knows (it warns). A different major is unsupported. Later minors only add fields at the end; offsets never move.

## Validation

A parsed descriptor is used only if it validates: `mod_id` not nil; `reverse_domain` is two or more dot-separated labels
of 1–63 characters `a-z 0-9 -` (not starting or ending with `-`), at most 96 bytes; `name` is 1–64 bytes without
control characters or leading/trailing spaces; `publisher` at most 48 and `homepage` at most 64 bytes, without control
characters; range minimums not above maximums; no critical extension unknown to the reader. A homepage not starting with
`http://` or `https://` is a warning.

`reverse_domain` is the same identifier as `id` in a mod's `me-mod.json` (MIRAGE's shared mod identity record).

## Extensions

The extension area holds records back to back, each:

| Size | Field |
|---:|---|
| 2 | type |
| 2 | flags (bit 0: critical) |
| 4 | payload size |
| n | payload |

Types 0x0001 publisher signature, 0x0002 dependencies, 0x0003 relationships, 0x0004 runtime interop, 0x0005 capability
details are reserved (no payload defined yet); 0x8000–0xFFFF are experimental. A reader keeps unknown records as they
are and refuses a descriptor with an unknown **critical** record. A record that runs past the area is an error.

## Custom metadata (`.memidx`)

The section holds one or more containers, possibly separated by zero padding. Container header (24 bytes):

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | magic `4D 45 4D 49 44 58 0D 0A` ("MEMIDX" CR LF) |
| 8 | 2 | version (1) |
| 10 | 2 | header size (24) |
| 12 | 4 | total size, header included |
| 16 | 4 | entry count |
| 20 | 4 | reserved (0) |

Entries follow the header (`header size` from its start), each:

| Size | Field |
|---:|---|
| 2 | namespace size |
| 2 | format (0 raw, 1 text, 2 JSON, 3 XML, 4 CBOR, 5 MessagePack, 6 TOML; 0x8000+ developer-defined) |
| 4 | payload size |
| 2 | flags (none defined; kept as read) |
| 2 | reserved (0) |
| n | namespace: a reverse-domain identifier, UTF-8, not terminated |
| m | payload |

See [CUSTOM_METADATA.md](CUSTOM_METADATA.md) for the rules readers follow.
