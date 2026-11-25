// Embedding a MEMID identity (and developer metadata) in the binary being built: the descriptor is computed at compile
// time and placed in the .memid section; nothing runs at startup and nothing is allocated. Header-only: a mod that
// only embeds its identity needs this header, not the library.
//
//   #include <memid/embed.hpp>
//
//   MEMID_DEFINE_IDENTITY(
//       .mod_id = "6f9619ff-8b86-d011-b42d-00c04fc964ff",
//       .reverse_domain = "com.example.wallrun-trails",
//       .name = "Wallrun Trails",
//       .publisher = "Example",
//       .version = "1.2.0");
//
//   MEMID_DEFINE_CUSTOM_TEXT(settings, "com.example.wallrun-trails", memid::MetadataFormat::json, R"({"trail":"blue"})");
//
// MEMID_DEFINE_IDENTITY takes designated initializers of memid::embed::Identity (below). It may be used once per binary:
// it also defines the exported C function memid_identity_descriptor, so a second use fails to link. Invalid values (a
// UUID or version that doesn't parse, text too long for its field) fail to compile.
//
// Retention: the descriptor is reached from the exported getter, so the linker keeps it in optimized builds;
// MEMID_DEFINE_CUSTOM_* data is marked used (and /INCLUDE'd on MSVC). Tested: MSVC and clang-cl, x86 and x64, PE.
#pragma once

#include <memid/custom_metadata.hpp>
#include <memid/detail/codec.hpp>
#include <memid/export.h>
#include <memid/format.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace memid::embed
{
	// What MEMID_DEFINE_IDENTITY fills: text fields as literals, the rest with defaults for the program being compiled
	struct Identity
	{
		std::string_view mod_id;				// Required: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
		std::string_view reverse_domain;		// Required: "com.example.mod"
		std::string_view name;					// Required
		std::string_view publisher = {};
		std::string_view version = "0.0.0";		// "MAJOR.MINOR.PATCH"
		std::string_view homepage = {};
		std::string_view publisher_id = {};		// A UUID, or empty
		std::string_view build_id = {};			// A UUID, or empty
		ReleaseChannel channel = ReleaseChannel::stable;
		Architecture architecture = detail::host_architecture();
		OperatingSystem os = detail::host_os();
		BinaryFormat binary_format = detail::host_binary_format();
		Language language = Language::cpp;
		Runtime runtime = Runtime::none;
		std::uint32_t game_id = game::mirrors_edge_2008_pc;
		std::uint32_t game_version_min = 0, game_version_max = 0;
		std::uint32_t interop_min = 0, interop_max = 0;
		std::uint64_t capabilities = 0;
		std::uint64_t protocol_flags = 0;		// protocol::c_abi_getter is always added
		std::uint32_t flags = 0;
		std::uint64_t build_timestamp = 0;		// Seconds since 1970 UTC; 0: unknown (builds stay reproducible)
		std::uint64_t toolchain_id = detail::host_toolchain();
	};

	namespace detail_
	{
		// A compile error with this text when evaluated in a constant expression
		inline void invalid(const char*) {}

		constexpr Uuid uuid_or_fail(std::string_view text, bool required, const char* what)
		{
			if (text.empty()) {
				if (required) invalid(what);
				return {};
			}
			const auto u = Uuid::parse(text);
			if (!u) invalid(what);
			return u ? *u : Uuid{};
		}
	}

	// The descriptor bytes for `id`, checked: used in a constant expression, a bad value doesn't compile
	constexpr detail::Bytes encode(const Identity& id)
	{
		detail::Fields f;
		f.flags = id.flags;
		f.mod_id = detail_::uuid_or_fail(id.mod_id, true, "MEMID: mod_id is missing or not a UUID");
		f.publisher_id = detail_::uuid_or_fail(id.publisher_id, false, "MEMID: publisher_id is not a UUID");
		f.build_id = detail_::uuid_or_fail(id.build_id, false, "MEMID: build_id is not a UUID");
		const auto version = Version::parse(id.version);
		if (!version) detail_::invalid("MEMID: version is not MAJOR.MINOR.PATCH");
		f.version = version ? *version : Version{};
		if (id.reverse_domain.empty() || id.name.empty()) detail_::invalid("MEMID: reverse_domain and name are required");
		f.reverse_domain = id.reverse_domain;
		f.name = id.name;
		f.publisher = id.publisher;
		f.homepage = id.homepage;
		if (!detail::fits(f)) detail_::invalid("MEMID: a text field is longer than its 96/64/48/64 bytes");
		f.release_channel = static_cast<std::uint16_t>(id.channel);
		f.architecture = static_cast<std::uint16_t>(id.architecture);
		f.operating_system = static_cast<std::uint16_t>(id.os);
		f.binary_format = static_cast<std::uint8_t>(id.binary_format);
		f.language = static_cast<std::uint8_t>(id.language);
		f.runtime = static_cast<std::uint16_t>(id.runtime);
		f.build_timestamp = id.build_timestamp;
		f.toolchain_id = id.toolchain_id;
		f.game_id = id.game_id;
		f.game_version_min = id.game_version_min;
		f.game_version_max = id.game_version_max;
		f.interop_min = id.interop_min;
		f.interop_max = id.interop_max;
		f.capabilities = id.capabilities;
		f.protocol_flags = id.protocol_flags | protocol::c_abi_getter;
		return detail::write(f);
	}
}

// ---- Placement in the .memid / .memidx sections --------------------------------------------------------------------
#if defined(_MSC_VER)
#  define MEMID_DETAIL_SECTIONS __pragma(section(".memid", read)) __pragma(section(".memidx", read))
#  define MEMID_DETAIL_IN(section) __declspec(allocate(section))
#  define MEMID_DETAIL_EXPORT __declspec(dllexport)
#  if defined(_M_IX86)
#    define MEMID_DETAIL_KEEP(symbol) __pragma(comment(linker, "/INCLUDE:_" #symbol))
#  else
#    define MEMID_DETAIL_KEEP(symbol) __pragma(comment(linker, "/INCLUDE:" #symbol))
#  endif
#elif defined(__GNUC__)
#  define MEMID_DETAIL_SECTIONS
#  if defined(__APPLE__)
#    define MEMID_DETAIL_IN(section) __attribute__((section("__DATA," section), used))
#  elif defined(__has_attribute) && __has_attribute(retain)
#    define MEMID_DETAIL_IN(section) __attribute__((section(section), used, retain))
#  else
#    define MEMID_DETAIL_IN(section) __attribute__((section(section), used))
#  endif
#  if defined(_WIN32)
#    define MEMID_DETAIL_EXPORT __declspec(dllexport)
#  else
#    define MEMID_DETAIL_EXPORT __attribute__((visibility("default")))
#  endif
#  define MEMID_DETAIL_KEEP(symbol)
#else
#  error "MEMID embedding supports MSVC, clang and GCC"
#endif

// The identity of this binary (once per binary). Also defines the exported C getter:
//   const unsigned char* memid_identity_descriptor(uint32_t* size)
#define MEMID_DEFINE_IDENTITY(...)                                                                                         \
	MEMID_DETAIL_SECTIONS                                                                                                  \
	extern "C" {                                                                                                           \
	MEMID_DETAIL_IN(".memid") extern const ::std::array<unsigned char, ::memid::kDescriptorSize> memid_identity_data;      \
	MEMID_DETAIL_IN(".memid") constinit const ::std::array<unsigned char, ::memid::kDescriptorSize> memid_identity_data =  \
		::memid::embed::encode(::memid::embed::Identity{__VA_ARGS__});                                                       \
	MEMID_DETAIL_EXPORT const unsigned char* MEMID_CALL memid_identity_descriptor(::std::uint32_t* size)                   \
	{                                                                                                                      \
		if (size) *size = ::memid::kDescriptorSize;                                                                        \
		return memid_identity_data.data();                                                                                 \
	}                                                                                                                      \
	}

// One custom metadata entry with a text payload (a string literal, stored without its terminating NUL). `id` names the
// variable (any identifier unique in the binary).
#define MEMID_DEFINE_CUSTOM_TEXT(id, namespace_id, format, text)                                                           \
	MEMID_DETAIL_SECTIONS                                                                                                  \
	extern "C" {                                                                                                           \
	MEMID_DETAIL_IN(".memidx") extern const decltype(::memid::detail::custom_text_container(                              \
		namespace_id, static_cast<::std::uint16_t>(format), text)) memid_custom_##id;                                      \
	MEMID_DETAIL_IN(".memidx") constinit const decltype(::memid::detail::custom_text_container(                           \
		namespace_id, static_cast<::std::uint16_t>(format), text)) memid_custom_##id =                                     \
		::memid::detail::custom_text_container(namespace_id, static_cast<::std::uint16_t>(format), text);                 \
	}                                                                                                                      \
	MEMID_DETAIL_KEEP(memid_custom_##id)

// The same with a byte array payload: `bytes` names a constexpr `unsigned char[]`
#define MEMID_DEFINE_CUSTOM_BYTES(id, namespace_id, format, bytes)                                                         \
	MEMID_DETAIL_SECTIONS                                                                                                  \
	extern "C" {                                                                                                           \
	MEMID_DETAIL_IN(".memidx") extern const decltype(::memid::detail::custom_bytes_container(                             \
		namespace_id, static_cast<::std::uint16_t>(format), bytes)) memid_custom_##id;                                     \
	MEMID_DETAIL_IN(".memidx") constinit const decltype(::memid::detail::custom_bytes_container(                          \
		namespace_id, static_cast<::std::uint16_t>(format), bytes)) memid_custom_##id =                                    \
		::memid::detail::custom_bytes_container(namespace_id, static_cast<::std::uint16_t>(format), bytes);               \
	}                                                                                                                      \
	MEMID_DETAIL_KEEP(memid_custom_##id)
