// A thread-safe table of the identities of loaded modules, keyed by mod UUID. Entries are immutable and shared: a
// lookup returns a shared_ptr that stays valid after the entry is unregistered, so nothing dangles.
#pragma once

#include <memid/descriptor.hpp>
#include <memid/export.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace memid
{
	struct RegisteredIdentity
	{
		Descriptor descriptor;
		std::string module;		// Who registered it: a file path or name, unique in the registry (may be empty)
	};
	using IdentityPtr = std::shared_ptr<const RegisteredIdentity>;

	enum class RegisterStatus { ok, invalid_descriptor, duplicate_mod_id, duplicate_module };

	struct RegisterResult
	{
		RegisterStatus status = RegisterStatus::ok;
		std::string message;
		IdentityPtr identity;	// ok: the new entry. duplicate_*: the one already there.
		explicit operator bool() const { return status == RegisterStatus::ok; }
	};

	class MEMID_API Registry
	{
	public:
		Registry() = default;
		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;

		// Registers `descriptor` for `module` if it validates and neither its mod id nor `module` is registered yet
		RegisterResult register_identity(Descriptor descriptor, std::string module = {});
		// Removes the entry with this mod id / module; false if there is none
		bool unregister_identity(const Uuid& mod_id);
		bool unregister_module(std::string_view module);
		void clear();

		IdentityPtr find_identity(const Uuid& mod_id) const;
		IdentityPtr find_module(std::string_view module) const;
		IdentityPtr find_reverse_domain(std::string_view id) const;
		std::vector<IdentityPtr> find_by_publisher(const Uuid& publisher_id) const;
		std::vector<IdentityPtr> find_by_name(std::string_view name) const;	// Exact, case-sensitive
		// Every entry, in no particular order, as of one moment
		std::vector<IdentityPtr> snapshot() const;
		std::size_t size() const;
		// Counts changes: equal values mean the registry didn't change in between
		std::uint64_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }

	private:
		mutable std::shared_mutex lock_;
		std::unordered_map<Uuid, IdentityPtr, UuidHash> byId_;
		std::unordered_map<std::string, Uuid> byModule_;
		std::atomic<std::uint64_t> generation_{0};
	};
}
