// memid-inspect: what MEMID data a binary holds, read without loading it.
//
//   memid-inspect [options] FILE
//     --json                       machine-readable output
//     --validate                   exit non-zero unless the identity (and custom metadata, if present) is valid
//     --extensions                 list the descriptor's extension records
//     --custom                     list the .memidx custom metadata entries
//     --custom-namespace NS        only entries of namespace NS (implies --custom)
//     --decode                     print text payloads (text, JSON, XML, TOML) with --custom
//     --export-payload NS FILE     write the first NS entry's raw payload to FILE
//     --version, --help
//
// Exit codes: 0 identified, 1 usage, 2 the file can't be read or isn't a supported binary, 3 no identity (a legacy
// binary), 4 the identity is malformed or invalid, 5 custom metadata problems (with --validate).

#include <memid/memid.hpp>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	struct Options
	{
		bool json = false, validate = false, extensions = false, custom = false, decode = false;
		std::string ns, exportNs, exportFile, file;
	};

	std::filesystem::path Utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

	std::string Escape(std::string_view s)
	{
		std::string out;
		for (const char c : s) {
			switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
					out += buf;
				} else
					out += c;
			}
		}
		return out;
	}
	std::string Q(std::string_view s) { return "\"" + Escape(s) + "\""; }

	const char* Name(memid::BinaryFormat f)
	{
		switch (f) {
		case memid::BinaryFormat::pe: return "PE";
		case memid::BinaryFormat::elf: return "ELF";
		case memid::BinaryFormat::macho: return "Mach-O";
		default: return "unknown";
		}
	}
	const char* Name(memid::Architecture a)
	{
		switch (a) {
		case memid::Architecture::x86: return "x86";
		case memid::Architecture::x86_64: return "x86-64";
		case memid::Architecture::arm: return "ARM";
		case memid::Architecture::arm64: return "ARM64";
		case memid::Architecture::any: return "any";
		default: return "unknown";
		}
	}
	const char* Name(memid::ReleaseChannel c)
	{
		switch (c) {
		case memid::ReleaseChannel::stable: return "stable";
		case memid::ReleaseChannel::beta: return "beta";
		case memid::ReleaseChannel::alpha: return "alpha";
		case memid::ReleaseChannel::development: return "development";
		case memid::ReleaseChannel::nightly: return "nightly";
		default: return "unspecified";
		}
	}
	std::string FormatName(std::uint16_t f)
	{
		static const char* const names[] = {"raw", "text", "json", "xml", "cbor", "msgpack", "toml"};
		if (f < std::size(names)) return names[f];
		return f >= memid::kFirstPrivateFormat ? "private:" + std::to_string(f) : "unknown:" + std::to_string(f);
	}
	bool TextFormat(std::uint16_t f)
	{
		using F = memid::MetadataFormat;
		return f == static_cast<std::uint16_t>(F::text) || f == static_cast<std::uint16_t>(F::json) ||
			   f == static_cast<std::uint16_t>(F::xml) || f == static_cast<std::uint16_t>(F::toml);
	}
	std::string GameVersion(std::uint32_t v)
	{
		if (!v) return "any";
		return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 0xFF) + "." + std::to_string(v & 0xFFFF);
	}

	void Usage(std::ostream& out)
	{
		out << "usage: memid-inspect [--json] [--validate] [--extensions] [--custom] [--custom-namespace NS] [--decode]\n"
			   "                     [--export-payload NS FILE] FILE\n";
	}

	std::vector<const memid::CustomEntry*> Entries(const memid::Inspection& i, const Options& o)
	{
		std::vector<const memid::CustomEntry*> out;
		for (const memid::CustomEntry& e : i.custom.metadata.entries())
			if (o.ns.empty() || e.namespace_id == o.ns) out.push_back(&e);
		return out;
	}

	void PrintText(const memid::Inspection& i, const Options& o)
	{
		std::cout << o.file << "\n  format: " << Name(i.format) << ", " << Name(i.architecture) << "\n";
		if (i.file_error != memid::ErrorCode::none) {
			std::cout << "  error: " << i.file_message << "\n";
			return;
		}
		if (const auto& d = i.descriptor) {
			std::cout << "  identity: " << d->name << " " << d->version.to_string() << " (" << Name(d->channel) << ")\n"
					  << "    id: " << d->reverse_domain << "\n    mod uuid: " << d->mod_id.to_string() << "\n";
			if (!d->publisher.empty() || !d->publisher_id.nil())
				std::cout << "    publisher: " << d->publisher << (d->publisher_id.nil() ? "" : " (" + d->publisher_id.to_string() + ")") << "\n";
			if (!d->homepage.empty()) std::cout << "    homepage: " << d->homepage << "\n";
			std::cout << "    format: " << d->format_major << "." << d->format_minor << ", built for " << Name(d->architecture) << "\n";
			if (d->game_id) std::cout << "    game: " << d->game_id << " (" << GameVersion(d->game_version_min) << " to " << GameVersion(d->game_version_max) << ")\n";
			if (d->interop_min || d->interop_max) std::cout << "    interop: " << d->interop_min << " to " << d->interop_max << "\n";
			std::cout << "    valid: " << (i.validation.ok() ? "yes" : "no") << "\n";
			for (const auto& issue : i.validation.issues)
				std::cout << "      " << (issue.severity == memid::Issue::Severity::error ? "error: " : "warning: ") << issue.message << "\n";
			for (const auto& w : i.identity_warnings) std::cout << "      warning: " << w.message << "\n";
			if (o.extensions) {
				std::cout << "    extensions: " << d->extensions.size() << "\n";
				for (const auto& e : d->extensions)
					std::cout << "      type " << e.type << (e.critical() ? " (critical)" : "") << ": " << e.payload.size() << " bytes\n";
			}
		} else if (i.identity_error == memid::ErrorCode::not_found)
			std::cout << "  identity: none (" << i.identity_message << ")\n";
		else
			std::cout << "  identity: unreadable: " << memid::to_string(i.identity_error) << ": " << i.identity_message << "\n";

		if (o.custom) {
			if (!i.has_custom_section) {
				std::cout << "  custom metadata: none\n";
				return;
			}
			std::cout << "  custom metadata: " << i.custom.metadata.size() << " entr" << (i.custom.metadata.size() == 1 ? "y" : "ies")
					  << " in " << i.custom.containers << " container(s), " << (i.custom.ok() ? "valid" : "with problems") << "\n";
			for (const auto& issue : i.custom.issues)
				std::cout << "    " << (issue.severity == memid::Issue::Severity::error ? "error: " : "warning: ") << issue.message << "\n";
			for (const memid::CustomEntry* e : Entries(i, o)) {
				std::cout << "    " << e->namespace_id << ": " << FormatName(e->format) << ", " << e->payload.size() << " bytes\n";
				if (o.decode && TextFormat(e->format) && memid::is_utf8(e->text())) std::cout << "      " << e->text() << "\n";
			}
		}
	}

	void PrintJson(const memid::Inspection& i, const Options& o)
	{
		std::ostringstream j;
		j << "{\"file\":" << Q(o.file) << ",\"binary_format\":" << Q(Name(i.format)) << ",\"architecture\":" << Q(Name(i.architecture));
		if (i.file_error != memid::ErrorCode::none) j << ",\"error\":" << Q(i.file_message);
		j << ",\"identity\":";
		if (const auto& d = i.descriptor) {
			j << "{\"mod_id\":" << Q(d->mod_id.to_string()) << ",\"reverse_domain\":" << Q(d->reverse_domain) << ",\"name\":" << Q(d->name)
			  << ",\"publisher\":" << Q(d->publisher) << ",\"publisher_id\":" << (d->publisher_id.nil() ? "null" : Q(d->publisher_id.to_string()))
			  << ",\"homepage\":" << Q(d->homepage) << ",\"version\":" << Q(d->version.to_string()) << ",\"channel\":" << Q(Name(d->channel))
			  << ",\"format\":" << Q(std::to_string(d->format_major) + "." + std::to_string(d->format_minor))
			  << ",\"architecture\":" << Q(Name(d->architecture)) << ",\"game_id\":" << d->game_id
			  << ",\"game_version_min\":" << d->game_version_min << ",\"game_version_max\":" << d->game_version_max
			  << ",\"interop_min\":" << d->interop_min << ",\"interop_max\":" << d->interop_max << ",\"capabilities\":" << d->capabilities
			  << ",\"protocol_flags\":" << d->protocol_flags << ",\"build_timestamp\":" << d->build_timestamp
			  << ",\"valid\":" << (i.validation.ok() ? "true" : "false") << ",\"issues\":[";
			bool first = true;
			for (const auto& issue : i.validation.issues) {
				j << (first ? "" : ",") << "{\"severity\":" << Q(issue.severity == memid::Issue::Severity::error ? "error" : "warning")
				  << ",\"message\":" << Q(issue.message) << "}";
				first = false;
			}
			j << "],\"extensions\":[";
			first = true;
			for (const auto& e : d->extensions) {
				j << (first ? "" : ",") << "{\"type\":" << e.type << ",\"flags\":" << e.flags << ",\"size\":" << e.payload.size() << "}";
				first = false;
			}
			j << "]}";
		} else
			j << "null";
		j << ",\"identity_status\":" << Q(memid::to_string(i.identity_error)) << ",\"identity_message\":" << Q(i.identity_message);
		if (o.custom) {
			j << ",\"custom\":";
			if (!i.has_custom_section)
				j << "null";
			else {
				j << "{\"containers\":" << i.custom.containers << ",\"valid\":" << (i.custom.ok() ? "true" : "false") << ",\"issues\":[";
				bool first = true;
				for (const auto& issue : i.custom.issues) {
					j << (first ? "" : ",") << Q(issue.message);
					first = false;
				}
				j << "],\"entries\":[";
				first = true;
				for (const memid::CustomEntry* e : Entries(i, o)) {
					j << (first ? "" : ",") << "{\"namespace\":" << Q(e->namespace_id) << ",\"format\":" << Q(FormatName(e->format))
					  << ",\"size\":" << e->payload.size();
					if (o.decode && TextFormat(e->format) && memid::is_utf8(e->text())) j << ",\"text\":" << Q(e->text());
					j << "}";
					first = false;
				}
				j << "]}";
			}
		}
		j << "}";
		std::cout << j.str() << "\n";
	}
}

int main(int argc, char** argv)
{
	Options o;
	for (int a = 1; a < argc; ++a) {
		const std::string arg = argv[a];
		if (arg == "--json") o.json = true;
		else if (arg == "--validate") o.validate = true;
		else if (arg == "--extensions") o.extensions = true;
		else if (arg == "--custom") o.custom = true;
		else if (arg == "--decode") o.decode = true;
		else if (arg == "--custom-namespace" && a + 1 < argc) o.custom = true, o.ns = argv[++a];
		else if (arg == "--export-payload" && a + 2 < argc) o.custom = true, o.exportNs = argv[++a], o.exportFile = argv[++a];
		else if (arg == "--version") {
			std::cout << "memid-inspect " << memid::kLibraryVersionMajor << "." << memid::kLibraryVersionMinor << "." << memid::kLibraryVersionPatch
					  << " (MEMID format " << memid::kFormatMajor << "." << memid::kFormatMinor << ")\n";
			return 0;
		} else if (arg == "--help" || arg == "-h") {
			Usage(std::cout);
			return 0;
		} else if (!arg.empty() && arg[0] == '-') {
			Usage(std::cerr);
			return 1;
		} else if (o.file.empty())
			o.file = arg;
		else {
			Usage(std::cerr);
			return 1;
		}
	}
	if (o.file.empty()) {
		Usage(std::cerr);
		return 1;
	}

	const memid::Inspection i = memid::inspect_file(Utf8Path(o.file));
	if (o.json)
		PrintJson(i, o);
	else
		PrintText(i, o);

	if (!o.exportNs.empty()) {
		const memid::CustomEntry* e = i.custom.metadata.find(o.exportNs);
		if (!e) {
			std::cerr << "no custom metadata entry of namespace " << o.exportNs << "\n";
			return 5;
		}
		std::ofstream out(Utf8Path(o.exportFile), std::ios::binary);
		out.write(reinterpret_cast<const char*>(e->payload.data()), static_cast<std::streamsize>(e->payload.size()));
		if (!out) {
			std::cerr << "can't write " << o.exportFile << "\n";
			return 2;
		}
	}

	if (i.file_error != memid::ErrorCode::none) return 2;
	if (!i.descriptor) return i.identity_error == memid::ErrorCode::not_found ? 3 : 4;
	if (o.validate && !i.validation.ok()) return 4;
	if (o.validate && i.has_custom_section && !i.custom.ok()) return 5;
	return 0;
}
