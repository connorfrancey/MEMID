// Developer-defined custom metadata (.memidx): the container's writer and its bounded reader.

#include <memid/custom_metadata.hpp>
#include <memid/descriptor.hpp>

#include <algorithm>

namespace memid
{
	using detail::get16;
	using detail::get32;
	using detail::put16;
	using detail::put32;

	bool is_namespace(std::string_view ns, std::size_t max) noexcept { return is_reverse_domain(ns, max); }

	void CustomMetadata::replace(CustomEntry entry)
	{
		remove(entry.namespace_id);
		entries_.push_back(std::move(entry));
	}

	std::size_t CustomMetadata::remove(std::string_view ns)
	{
		const auto before = entries_.size();
		entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const CustomEntry& e) { return e.namespace_id == ns; }),
					   entries_.end());
		return before - entries_.size();
	}

	const CustomEntry* CustomMetadata::find(std::string_view ns) const noexcept
	{
		for (const CustomEntry& e : entries_)
			if (e.namespace_id == ns) return &e;
		return nullptr;
	}

	std::vector<const CustomEntry*> CustomMetadata::find_all(std::string_view ns) const
	{
		std::vector<const CustomEntry*> out;
		for (const CustomEntry& e : entries_)
			if (e.namespace_id == ns) out.push_back(&e);
		return out;
	}

	ValidationResult CustomMetadata::validate(const Limits& limits) const
	{
		ValidationResult v;
		const auto error = [&v](ErrorCode code, std::string m) { v.issues.push_back({Issue::Severity::error, code, std::move(m)}); };
		if (entries_.size() > limits.max_entries) error(ErrorCode::limit_exceeded, std::to_string(entries_.size()) + " entries");
		std::uint64_t total = kCustomHeaderSize;
		for (std::size_t i = 0; i < entries_.size(); ++i) {
			const CustomEntry& e = entries_[i];
			const std::string at = "entry " + std::to_string(i) + " (" + e.namespace_id + ")";
			if (!is_namespace(e.namespace_id, std::min<std::size_t>(limits.max_namespace, 0xFFFF)))
				error(ErrorCode::invalid_field, at + ": the namespace must be a reverse-domain identifier of at most " +
													std::to_string(limits.max_namespace) + " bytes");
			if (e.payload.size() > limits.max_payload)
				error(ErrorCode::limit_exceeded, at + ": " + std::to_string(e.payload.size()) + "-byte payload");
			if ((e.format == static_cast<std::uint16_t>(MetadataFormat::text) || e.format == static_cast<std::uint16_t>(MetadataFormat::json) ||
				 e.format == static_cast<std::uint16_t>(MetadataFormat::toml) || e.format == static_cast<std::uint16_t>(MetadataFormat::xml)) &&
				!is_utf8(e.text()))
				error(ErrorCode::invalid_field, at + ": a text format whose payload isn't UTF-8");
			total += kCustomEntryHeaderSize + e.namespace_id.size() + e.payload.size();
		}
		if (total > limits.max_custom_section) error(ErrorCode::limit_exceeded, "the container is " + std::to_string(total) + " bytes");
		return v;
	}

	std::vector<std::uint8_t> CustomMetadata::serialize() const
	{
		if (entries_.empty()) return {};
		std::vector<std::uint8_t> out(kCustomHeaderSize);
		std::copy(kCustomMagic.begin(), kCustomMagic.end(), out.begin());
		put16(&out[8], kCustomVersion);
		put16(&out[10], static_cast<std::uint16_t>(kCustomHeaderSize));
		put32(&out[16], static_cast<std::uint32_t>(entries_.size()));
		for (const CustomEntry& e : entries_) {
			std::uint8_t h[kCustomEntryHeaderSize]{};
			put16(h, static_cast<std::uint16_t>(e.namespace_id.size()));
			put16(h + 2, e.format);
			put32(h + 4, static_cast<std::uint32_t>(e.payload.size()));
			put16(h + 8, e.flags);
			out.insert(out.end(), h, h + kCustomEntryHeaderSize);
			out.insert(out.end(), e.namespace_id.begin(), e.namespace_id.end());
			out.insert(out.end(), e.payload.begin(), e.payload.end());
		}
		put32(&out[12], static_cast<std::uint32_t>(out.size()));
		return out;
	}

	CustomParse parse_custom_section(std::span<const std::uint8_t> section, const Limits& limits)
	{
		CustomParse r;
		const auto error = [&r](ErrorCode code, std::string m) { r.issues.push_back({Issue::Severity::error, code, std::move(m)}); };
		const auto warn = [&r](std::string m) { r.issues.push_back({Issue::Severity::warning, ErrorCode::none, std::move(m)}); };
		if (section.size() > limits.max_custom_section) {
			error(ErrorCode::limit_exceeded, "the section is " + std::to_string(section.size()) + " bytes (at most " +
												 std::to_string(limits.max_custom_section) + " read)");
			section = section.first(limits.max_custom_section);
		}
		std::size_t at = 0, entries = 0;
		while (at < section.size()) {
			if (section[at] == 0) {	// Padding between containers
				++at;
				continue;
			}
			const std::size_t left = section.size() - at;
			const std::uint8_t* p = section.data() + at;
			if (left < kCustomHeaderSize || !std::equal(kCustomMagic.begin(), kCustomMagic.end(), p)) {
				error(ErrorCode::bad_magic, "byte " + std::to_string(at) + ": not a custom metadata container; the rest is skipped");
				break;
			}
			const std::uint16_t version = get16(p + 8), headerSize = get16(p + 10);
			const std::uint32_t total = get32(p + 12), count = get32(p + 16);
			if (version != kCustomVersion) {
				error(ErrorCode::unsupported_version, "container at byte " + std::to_string(at) + ": version " + std::to_string(version));
				if (total >= kCustomHeaderSize && total <= left) {	// Its size is still usable to skip it
					at += total;
					continue;
				}
				break;
			}
			if (headerSize < kCustomHeaderSize || total < headerSize || total > left) {
				error(ErrorCode::malformed, "container at byte " + std::to_string(at) + ": header size " + std::to_string(headerSize) +
												", total size " + std::to_string(total) + " with " + std::to_string(left) + " bytes left");
				break;
			}
			if (get32(p + 20) != 0) warn("container at byte " + std::to_string(at) + ": reserved field is not zero");
			++r.containers;
			std::size_t e = headerSize;
			for (std::uint32_t n = 0; n < count; ++n) {
				if (entries >= limits.max_entries) {
					error(ErrorCode::limit_exceeded, "more than " + std::to_string(limits.max_entries) + " entries");
					return r;
				}
				const std::string where = "container at byte " + std::to_string(at) + ", entry " + std::to_string(n);
				if (total - e < kCustomEntryHeaderSize) {
					error(ErrorCode::truncated, where + ": its header runs past the container");
					break;
				}
				const std::uint8_t* h = p + e;
				const std::uint16_t nsSize = get16(h), format = get16(h + 2), flags = get16(h + 8);
				const std::uint32_t payloadSize = get32(h + 4);
				const std::uint64_t need = std::uint64_t{kCustomEntryHeaderSize} + nsSize + payloadSize;
				if (need > total - e) {
					error(ErrorCode::truncated, where + ": namespace and payload run past the container");
					break;
				}
				const std::string_view ns(reinterpret_cast<const char*>(h + kCustomEntryHeaderSize), nsSize);
				e += static_cast<std::size_t>(need);
				++entries;
				if (nsSize > limits.max_namespace || !is_namespace(ns, limits.max_namespace)) {
					error(ErrorCode::invalid_field, where + ": the namespace is not a reverse-domain identifier; skipped");
					continue;
				}
				if (payloadSize > limits.max_payload) {
					error(ErrorCode::limit_exceeded, where + " (" + std::string(ns) + "): " + std::to_string(payloadSize) + "-byte payload; skipped");
					continue;
				}
				if (get16(h + 10) != 0) warn(where + ": reserved field is not zero");
				CustomEntry entry;
				entry.namespace_id.assign(ns);
				entry.format = format;
				entry.flags = flags;
				const std::uint8_t* payload = h + kCustomEntryHeaderSize + nsSize;
				entry.payload.assign(payload, payload + payloadSize);
				r.metadata.add(std::move(entry));
			}
			if (e != total) warn("container at byte " + std::to_string(at) + ": " + std::to_string(total - e) + " bytes after its entries");
			at += total;
		}
		return r;
	}
}
