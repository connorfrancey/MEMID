# Compatibility

| Layer | Rule |
|---|---|
| **Format** | Major 1 is stable: offsets and meanings never change. A minor may append fields (`descriptor_size` grows); readers ignore what they don't know. A breaking change is a new major, which 1.x readers refuse as unsupported. |
| **Custom container** | Version 1. Unknown versions are skipped by their size. |
| **Extensions** | New record types may appear at any time; unknown ones are kept, unknown *critical* ones refuse the descriptor. |
| **C ABI** | `MEMID_C_ABI_VERSION` 1. Functions are only added; structures grow at the end behind `struct_size`. |
| **C++ API** | Source-compatible within a library major version. The shared library's C++ exports require the same compiler and standard library on both sides; use the C ABI across compilers. |
| **CMake package** | `MEMIDConfigVersion.cmake`: same major version. |

## Legacy MMID

There is no earlier MMID binary format to read: before MEMID 1.1 only a layout table existed
(`schemas/memid-v1.1-layout.json`), and no binaries were produced with it. MEMID 1.1 keeps every offset that table
defined and assigns the bytes it left open (54–57 version minor and patch, 62–67 OS, binary format, language and
runtime, 104–127 game and interop ranges and a reserved block). No `.mmid` reader is provided.
