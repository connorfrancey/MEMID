// The descriptor's byte-level writer, constexpr so memid/embed.hpp builds descriptors at compile time and the library's
// serializer (descriptor.cpp) writes the same bytes at run time. Internal: use memid::serialize or MEMID_DEFINE_IDENTITY.
#pragma once

#include <memid/format.hpp>
#include <memid/uuid.hpp>
#include <memid/version.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace memid::detail
{
	using Bytes = std::array<std::uint8_t, kDescriptorSize>;

	constexpr void put16(std::uint8_t* p, std::uint16_t v)
	{
		p[0] = static_cast<std::uint8_t>(v);
		p[1] = static_cast<std::uint8_t>(v >> 8);
	}
	constexpr void put32(std::uint8_t* p, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
	}
	constexpr void put64(std::uint8_t* p, std::uint64_t v)
	{
		for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
	}
	constexpr std::uint16_t get16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
	constexpr std::uint32_t get32(const std::uint8_t* p)
	{
		std::uint32_t v = 0;
		for (int i = 0; i < 4; ++i) v |= std::uint32_t{p[i]} << (8 * i);
		return v;
	}
	constexpr std::uint64_t get64(const std::uint8_t* p)
	{
		std::uint64_t v = 0;
		for (int i = 0; i < 8; ++i) v |= std::uint64_t{p[i]} << (8 * i);
		return v;
	}

	// Every descriptor field, with text as views (a literal at compile time, a std::string at run time)
	struct Fields
	{
		std::uint16_t format_major = kFormatMajor, format_minor = kFormatMinor;
		std::uint32_t flags = 0;
		Uuid mod_id{}, publisher_id{};
		Version version{};
		std::uint16_t release_channel = 0, architecture = 0, operating_system = 0;
		std::uint8_t binary_format = 0, language = 0;
		std::uint16_t runtime = 0;
		Uuid build_id{};
		std::uint64_t build_timestamp = 0, toolchain_id = 0;
		std::uint32_t game_id = 0, game_version_min = 0, game_version_max = 0, interop_min = 0, interop_max = 0;
		std::uint64_t capabilities = 0, protocol_flags = 0;
		std::string_view reverse_domain, name, publisher, homepage;
		std::uint32_t extension_offset = 0, extension_size = 0, extension_flags = 0;
	};

	// False when a text doesn't fit its field (callers check first; write() then truncates nothing)
	constexpr bool fits(const Fields& f)
	{
		return f.reverse_domain.size() <= field_size::reverse_domain && f.name.size() <= field_size::name &&
			   f.publisher.size() <= field_size::publisher && f.homepage.size() <= field_size::homepage;
	}

	constexpr Bytes write(const Fields& f)
	{
		Bytes b{};
		for (std::size_t i = 0; i < kMagic.size(); ++i) b[offsets::magic + i] = kMagic[i];
		put16(&b[offsets::format_major], f.format_major);
		put16(&b[offsets::format_minor], f.format_minor);
		put32(&b[offsets::descriptor_size], kDescriptorSize);
		put32(&b[offsets::flags], f.flags);
		for (std::size_t i = 0; i < 16; ++i) {
			b[offsets::mod_id + i] = f.mod_id.bytes[i];
			b[offsets::publisher_id + i] = f.publisher_id.bytes[i];
			b[offsets::build_id + i] = f.build_id.bytes[i];
		}
		put16(&b[offsets::version_major], f.version.major);
		put16(&b[offsets::version_minor], f.version.minor);
		put16(&b[offsets::version_patch], f.version.patch);
		put16(&b[offsets::release_channel], f.release_channel);
		put16(&b[offsets::architecture], f.architecture);
		put16(&b[offsets::operating_system], f.operating_system);
		b[offsets::binary_format] = f.binary_format;
		b[offsets::language] = f.language;
		put16(&b[offsets::runtime], f.runtime);
		put64(&b[offsets::build_timestamp], f.build_timestamp);
		put64(&b[offsets::toolchain_id], f.toolchain_id);
		put32(&b[offsets::game_id], f.game_id);
		put32(&b[offsets::game_version_min], f.game_version_min);
		put32(&b[offsets::game_version_max], f.game_version_max);
		put32(&b[offsets::interop_min], f.interop_min);
		put32(&b[offsets::interop_max], f.interop_max);
		put64(&b[offsets::capabilities], f.capabilities);
		put64(&b[offsets::protocol_flags], f.protocol_flags);
		const auto text = [&b](std::size_t at, std::size_t size, std::string_view s) {
			for (std::size_t i = 0; i < s.size() && i < size; ++i) b[at + i] = static_cast<std::uint8_t>(s[i]);
		};
		text(offsets::reverse_domain, field_size::reverse_domain, f.reverse_domain);
		text(offsets::name, field_size::name, f.name);
		text(offsets::publisher, field_size::publisher, f.publisher);
		text(offsets::homepage, field_size::homepage, f.homepage);
		put32(&b[offsets::extension_offset], f.extension_offset);
		put32(&b[offsets::extension_size], f.extension_size);
		put32(&b[offsets::extension_flags], f.extension_flags);
		return b;
	}

	// The platform this translation unit is compiled for (an embedded descriptor's defaults)
	constexpr Architecture host_architecture()
	{
#if defined(_M_X64) || defined(__x86_64__)
		return Architecture::x86_64;
#elif defined(_M_IX86) || defined(__i386__)
		return Architecture::x86;
#elif defined(_M_ARM64) || defined(__aarch64__)
		return Architecture::arm64;
#elif defined(_M_ARM) || defined(__arm__)
		return Architecture::arm;
#else
		return Architecture::unknown;
#endif
	}
	constexpr OperatingSystem host_os()
	{
#if defined(_WIN32)
		return OperatingSystem::windows;
#elif defined(__APPLE__)
		return OperatingSystem::macos;
#elif defined(__linux__)
		return OperatingSystem::linux_;
#else
		return OperatingSystem::unknown;
#endif
	}
	constexpr BinaryFormat host_binary_format()
	{
#if defined(_WIN32)
		return BinaryFormat::pe;
#elif defined(__APPLE__)
		return BinaryFormat::macho;
#elif defined(__ELF__)
		return BinaryFormat::elf;
#else
		return BinaryFormat::unknown;
#endif
	}
	constexpr std::uint64_t host_toolchain()
	{
#if defined(__clang__) && defined(_MSC_VER)
		return toolchain::make(toolchain::clang_cl, __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__clang__)
		return toolchain::make(toolchain::clang, __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
		return toolchain::make(toolchain::msvc, _MSC_VER / 100, _MSC_VER % 100, static_cast<std::uint16_t>(_MSC_FULL_VER % 100000));
#elif defined(__GNUC__)
		return toolchain::make(toolchain::gcc, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
		return 0;
#endif
	}
}
