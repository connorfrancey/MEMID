# Security

**A MEMID descriptor identifies a binary; it does not authenticate it.** Anyone can embed any UUID, name or publisher.
A valid descriptor proves nothing about who built the binary, whether it is safe, or whether it is the release it
claims to be. Tools must not grant trust, permissions or automatic loading on the strength of a descriptor, and must
not let a new binary replace an installed one just because their ids match.

What the library guarantees as a reader of untrusted files:

- Inspection never loads, maps for execution or runs the binary: it reads the header, the section table and the two
  sections through bounds-checked reads.
- Every offset and size from the file is checked against the data before use, in 64-bit arithmetic where a sum could
  overflow 32 bits. Section, entry, namespace, payload and extension sizes are capped (`memid::Limits`).
- Text is never assumed to be NUL-terminated; it is length-delimited and validated as UTF-8.
- Custom payloads are opaque bytes: never decoded unless asked, never executed.
- The tests feed truncated and randomly corrupted PE and ELF files to the inspector (`tests/test_inspector.cpp`).

Publisher signatures (extension type 0x0001) are reserved. When they are defined, they will use an established
signature scheme and library and a documented verification model; MEMID will not invent cryptography.
