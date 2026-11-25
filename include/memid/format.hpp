// The MEMID binary format: magic numbers, versions, the v1.1 descriptor's field offsets and sizes, the enumerations
// stored in it, and the limits readers enforce. docs/MEMID_FORMAT.md describes the same layout in prose;
// tests/test_descriptor.cpp checks every offset against schemas/memid-v1.1-layout.json.
//
// Everything is little-endian and byte-packed. Nothing here depends on the compiler's structure layout: descriptors
// are written and read field by field (memid/detail/codec.hpp).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace memid
{
	// ---- The standardized descriptor (.memid) ----------------------------------------------------------------------
	// "MEMID", then CR LF and SUB, as PNG does: a file mangled by a text-mode transfer no longer matches
	inline constexpr std::array<std::uint8_t, 8> kMagic = {'M', 'E', 'M', 'I', 'D', 0x0D, 0x0A, 0x1A};
	inline constexpr std::uint16_t kFormatMajor = 1;
	inline constexpr std::uint16_t kFormatMinor = 1;
	inline constexpr std::uint32_t kDescriptorSize = 428;	// Format 1.1. Later 1.x minors may only grow it.

	// The section names (PE: exactly these; ELF: these section names; Mach-O: not supported yet)
	inline constexpr char kIdentitySection[] = ".memid";
	inline constexpr char kCustomSection[] = ".memidx";

	// Byte offsets of every v1.1 field from the start of the descriptor
	namespace offsets
	{
		inline constexpr std::size_t magic = 0;					// 8 bytes
		inline constexpr std::size_t format_major = 8;			// u16
		inline constexpr std::size_t format_minor = 10;			// u16
		inline constexpr std::size_t descriptor_size = 12;		// u32
		inline constexpr std::size_t flags = 16;				// u32
		inline constexpr std::size_t mod_id = 20;				// UUID, 16 bytes
		inline constexpr std::size_t publisher_id = 36;			// UUID, 16 bytes
		inline constexpr std::size_t version_major = 52;		// u16
		inline constexpr std::size_t version_minor = 54;		// u16
		inline constexpr std::size_t version_patch = 56;		// u16
		inline constexpr std::size_t release_channel = 58;		// u16
		inline constexpr std::size_t architecture = 60;			// u16
		inline constexpr std::size_t operating_system = 62;		// u16
		inline constexpr std::size_t binary_format = 64;		// u8
		inline constexpr std::size_t language = 65;				// u8
		inline constexpr std::size_t runtime = 66;				// u16
		inline constexpr std::size_t build_id = 68;				// UUID, 16 bytes
		inline constexpr std::size_t build_timestamp = 84;		// u64, seconds since 1970-01-01 UTC (0: unknown)
		inline constexpr std::size_t toolchain_id = 92;			// u64 (see Toolchain)
		inline constexpr std::size_t game_id = 100;				// u32
		inline constexpr std::size_t game_version_min = 104;	// u32, packed (pack_game_version)
		inline constexpr std::size_t game_version_max = 108;	// u32, packed
		inline constexpr std::size_t interop_min = 112;			// u32
		inline constexpr std::size_t interop_max = 116;			// u32
		inline constexpr std::size_t reserved = 120;			// 8 bytes, zero
		inline constexpr std::size_t capabilities = 128;		// u64
		inline constexpr std::size_t protocol_flags = 136;		// u64
		inline constexpr std::size_t reverse_domain = 144;		// 96 bytes, UTF-8, NUL-padded
		inline constexpr std::size_t name = 240;				// 64 bytes
		inline constexpr std::size_t publisher = 304;			// 48 bytes
		inline constexpr std::size_t homepage = 352;			// 64 bytes
		inline constexpr std::size_t extension_offset = 416;	// u32, from the start of the descriptor (0: none)
		inline constexpr std::size_t extension_size = 420;		// u32
		inline constexpr std::size_t extension_flags = 424;		// u32
	}

	// Sizes of the text fields: UTF-8, padded with NUL bytes. A value may fill its field completely (no terminator).
	namespace field_size
	{
		inline constexpr std::size_t reverse_domain = 96;
		inline constexpr std::size_t name = 64;
		inline constexpr std::size_t publisher = 48;
		inline constexpr std::size_t homepage = 64;
		inline constexpr std::size_t reserved = 8;
	}

	// Descriptor flags (u32). Bits not named here are reserved: writers leave them zero, readers ignore them.
	namespace flags
	{
		inline constexpr std::uint32_t debug_build = 1u << 0;		// Built without optimization, for development
		inline constexpr std::uint32_t has_custom_metadata = 1u << 1;	// A hint: the binary also has a .memidx section
	}

	// Protocol flags (u64): how the binary can be talked to
	namespace protocol
	{
		inline constexpr std::uint64_t c_abi_getter = 1ull << 0;	// Exports memid_identity_descriptor (memid/embed.hpp)
		inline constexpr std::uint64_t mirage_api = 1ull << 1;		// Speaks Mirage's mod ABI (interop range: its versions)
	}

	// Capabilities (u64): what the binary declares it does. Declarations, not permissions.
	namespace capability
	{
		inline constexpr std::uint64_t gameplay = 1ull << 0;
		inline constexpr std::uint64_t rendering = 1ull << 1;
		inline constexpr std::uint64_t user_interface = 1ull << 2;
		inline constexpr std::uint64_t audio = 1ull << 3;
		inline constexpr std::uint64_t input = 1ull << 4;
		inline constexpr std::uint64_t networking = 1ull << 5;
		inline constexpr std::uint64_t filesystem = 1ull << 6;
		inline constexpr std::uint64_t scripting = 1ull << 7;
		inline constexpr std::uint64_t content = 1ull << 8;
		inline constexpr std::uint64_t native_hooks = 1ull << 9;		// Patches or detours the game's code
		inline constexpr std::uint64_t tooling = 1ull << 10;			// A tool outside the game (launcher, editor)
	}

	enum class ReleaseChannel : std::uint16_t { unspecified = 0, stable = 1, beta = 2, alpha = 3, development = 4, nightly = 5 };
	enum class Architecture : std::uint16_t { unknown = 0, x86 = 1, x86_64 = 2, arm = 3, arm64 = 4, any = 0xFFFF };
	enum class OperatingSystem : std::uint16_t { unknown = 0, windows = 1, linux_ = 2, macos = 3, any = 0xFFFF };
	enum class BinaryFormat : std::uint8_t { unknown = 0, pe = 1, elf = 2, macho = 3 };
	enum class Language : std::uint8_t { unknown = 0, c = 1, cpp = 2, csharp = 3, rust = 4, zig = 5, d = 6, go = 7, pascal = 8, other = 255 };
	enum class Runtime : std::uint16_t { none = 0, mirage = 1, dotnet = 2, lua = 3, other = 0xFFFF };

	// Games (u32). 0: not tied to a game.
	namespace game
	{
		inline constexpr std::uint32_t none = 0;
		inline constexpr std::uint32_t mirrors_edge_2008_pc = 1;	// "mirrors-edge-2008-pc" in me-mod.json
	}

	// A game version as a u32: major in the top byte, minor in the next, patch in the low 16 bits. 0 as a range end means
	// "no limit" on that side.
	constexpr std::uint32_t pack_game_version(std::uint8_t major, std::uint8_t minor, std::uint16_t patch)
	{
		return (std::uint32_t{major} << 24) | (std::uint32_t{minor} << 16) | patch;
	}

	// The toolchain (u64): compiler family in the top 16 bits, then its major, minor and patch version (16 bits each).
	namespace toolchain
	{
		inline constexpr std::uint16_t unknown = 0, msvc = 1, clang = 2, gcc = 3, clang_cl = 4, rustc = 5, dotnet = 6;
		constexpr std::uint64_t make(std::uint16_t family, std::uint16_t major, std::uint16_t minor, std::uint16_t patch)
		{
			return (std::uint64_t{family} << 48) | (std::uint64_t{major} << 32) | (std::uint64_t{minor} << 16) | patch;
		}
	}

	// ---- Standardized extensions (TLV records after the descriptor) ----------------------------------------------------
	inline constexpr std::size_t kExtensionHeaderSize = 8;	// type u16, flags u16, payload size u32
	namespace extension_type
	{
		inline constexpr std::uint16_t publisher_signature = 0x0001;	// Reserved
		inline constexpr std::uint16_t dependencies = 0x0002;			// Reserved
		inline constexpr std::uint16_t relationships = 0x0003;			// Reserved
		inline constexpr std::uint16_t runtime_interop = 0x0004;		// Reserved
		inline constexpr std::uint16_t capability_details = 0x0005;		// Reserved
		inline constexpr std::uint16_t first_private = 0x8000;			// 0x8000-0xFFFF: experimental, not standardized
	}
	namespace extension_flag
	{
		// A reader that doesn't know the record's type must not use the descriptor (it can't honour the record)
		inline constexpr std::uint16_t critical = 1u << 0;
	}

	// ---- Developer-defined custom metadata (.memidx) ------------------------------------------------------------------
	inline constexpr std::array<std::uint8_t, 8> kCustomMagic = {'M', 'E', 'M', 'I', 'D', 'X', 0x0D, 0x0A};
	inline constexpr std::uint16_t kCustomVersion = 1;
	inline constexpr std::size_t kCustomHeaderSize = 24;		// magic 8, version u16, header size u16, total size u32,
																// entry count u32, reserved u32
	inline constexpr std::size_t kCustomEntryHeaderSize = 12;	// namespace size u16, format u16, payload size u32,
																// flags u16, reserved u16

	// Recommended payload formats (u16). 0x8000-0xFFFF: the developer's own.
	enum class MetadataFormat : std::uint16_t { raw = 0, text = 1, json = 2, xml = 3, cbor = 4, msgpack = 5, toml = 6 };
	inline constexpr std::uint16_t kFirstPrivateFormat = 0x8000;

	// Reader limits for untrusted input
	struct Limits
	{
		std::size_t max_identity_section = 64 * 1024;		// .memid: descriptor and its extensions
		std::size_t max_custom_section = 4 * 1024 * 1024;	// .memidx, all containers
		std::size_t max_entries = 4096;						// Custom entries, all containers
		std::size_t max_namespace = 128;					// Bytes
		std::size_t max_payload = 1024 * 1024;				// Bytes, per entry
		std::size_t max_extensions = 256;
	};
}
