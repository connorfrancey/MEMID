# Custom metadata (`.memidx`)

The `.memid` descriptor holds only the standardized identity. Anything else a developer wants in the binary goes in
`.memidx`: any number of entries, each with a **namespace** (whose data it is), a **format** (how to read it) and a
**payload** (bytes). MEMID defines the container, not the meaning of payloads. The byte layout is in
[MEMID_FORMAT.md](MEMID_FORMAT.md#custom-metadata-memidx).

## Rules readers follow

- No `.memidx` section is valid: custom metadata is optional.
- Containers may be separated by zero bytes. Several containers, and several entries with the same namespace, are
  allowed; order is kept. `find(ns)` returns the first entry of a namespace, `find_all(ns)` every one.
- A namespace is a reverse-domain identifier (`com.example.mymod`), 1–128 bytes. An entry whose namespace breaks this is
  skipped and reported; the others are kept.
- Unknown formats and flags are kept as they are: a generic reader exposes namespace, format, flags and raw bytes and
  never needs to understand a payload. Payloads are only decoded when a reader asks to (`memid-inspect --decode` prints
  text formats) and are never executed.
- A container whose sizes don't add up stops the parse at that point; entries read before it are kept. An unknown
  container version is skipped by its size.
- Problems in `.memidx` are reported separately (`Inspection::custom.issues`) and never make the identity unreadable.

## Limits (memid::Limits, adjustable)

| Limit | Default |
|---|---|
| Section size read | 4 MiB |
| Entries | 4096 |
| Namespace | 128 bytes |
| Payload | 1 MiB per entry |

## API

```cpp
memid::CustomMetadata m;
m.add({"com.example.mymod", memid::MetadataFormat::json, std::string_view(R"({"a":1})")});
m.replace({"com.example.mymod", memid::MetadataFormat::text, std::string_view("replaces every entry of that namespace")});
m.remove("com.example.mymod");
std::vector<std::uint8_t> bytes = m.serialize();             // one container

memid::CustomParse p = memid::parse_custom_section(bytes);  // p.metadata, p.issues, p.containers
if (const memid::CustomEntry* e = p.metadata.find("com.example.mymod")) use(e->payload);
```

`CustomMetadata` owns its entries; pointers from `find` stay valid until it is changed. Embedding: `MEMID_DEFINE_CUSTOM_TEXT`,
`MEMID_DEFINE_CUSTOM_BYTES` and `memid_add_custom_metadata` ([EMBEDDING.md](EMBEDDING.md)).

## Example schema

A mod that ships its default settings:

```json
{ "trail_color": "blue", "max_trails": 8 }
```

stored as namespace `com.example.minimal-mod.settings`, format JSON. Only that mod (or tools that know its namespace)
interprets it.

Custom metadata is untrusted input: being inside a binary doesn't authenticate it ([SECURITY.md](SECURITY.md)).
