// Standardized extension records (TLV) after the descriptor.

#include <memid/detail/codec.hpp>
#include <memid/extensions.hpp>

namespace memid
{
	bool is_known_extension(std::uint16_t type) noexcept
	{
		return type >= extension_type::publisher_signature && type <= extension_type::capability_details;
	}

	ExtensionParse parse_extensions(std::span<const std::uint8_t> data, const Limits& limits)
	{
		ExtensionParse r;
		std::size_t at = 0;
		while (at < data.size()) {
			if (r.records.size() >= limits.max_extensions) {
				r.error = ErrorCode::limit_exceeded;
				r.message = "more than " + std::to_string(limits.max_extensions) + " records";
				return r;
			}
			if (data.size() - at < kExtensionHeaderSize) {
				r.error = ErrorCode::truncated;
				r.message = "a record header at byte " + std::to_string(at) + " runs past the end";
				return r;
			}
			const std::uint8_t* p = data.data() + at;
			Extension e;
			e.type = detail::get16(p);
			e.flags = detail::get16(p + 2);
			const std::uint32_t size = detail::get32(p + 4);
			if (size > data.size() - at - kExtensionHeaderSize) {	// No overflow: the right side is the bytes left
				r.error = ErrorCode::truncated;
				r.message = "record " + std::to_string(r.records.size()) + " (type " + std::to_string(e.type) + ") claims " +
							std::to_string(size) + " bytes";
				return r;
			}
			e.payload.assign(p + kExtensionHeaderSize, p + kExtensionHeaderSize + size);
			r.records.push_back(std::move(e));
			at += kExtensionHeaderSize + size;
		}
		return r;
	}

	std::vector<std::uint8_t> serialize_extensions(const std::vector<Extension>& records)
	{
		std::vector<std::uint8_t> out;
		for (const Extension& e : records) {
			std::uint8_t header[kExtensionHeaderSize];
			detail::put16(header, e.type);
			detail::put16(header + 2, e.flags);
			detail::put32(header + 4, static_cast<std::uint32_t>(e.payload.size()));
			out.insert(out.end(), header, header + kExtensionHeaderSize);
			out.insert(out.end(), e.payload.begin(), e.payload.end());
		}
		return out;
	}

	const Extension* find_extension(const std::vector<Extension>& records, std::uint16_t type) noexcept
	{
		for (const Extension& e : records)
			if (e.type == type) return &e;
		return nullptr;
	}
}
