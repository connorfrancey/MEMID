// The identity registry: one shared_mutex around two maps; entries are immutable shared_ptrs.

#include <memid/registry.hpp>

#include <mutex>

namespace memid
{
	RegisterResult Registry::register_identity(Descriptor descriptor, std::string module)
	{
		RegisterResult r;
		if (const ValidationResult v = validate(descriptor); !v.ok()) {
			r.status = RegisterStatus::invalid_descriptor;
			for (const Issue& i : v.issues)
				if (i.severity == Issue::Severity::error) r.message += (r.message.empty() ? "" : "; ") + i.message;
			return r;
		}
		auto entry = std::make_shared<const RegisteredIdentity>(RegisteredIdentity{std::move(descriptor), std::move(module)});
		std::unique_lock lock(lock_);
		if (const auto it = byId_.find(entry->descriptor.mod_id); it != byId_.end()) {
			r.status = RegisterStatus::duplicate_mod_id;
			r.message = "mod id " + entry->descriptor.mod_id.to_string() + " is already registered by " +
						(it->second->module.empty() ? std::string("another module") : it->second->module);
			r.identity = it->second;
			return r;
		}
		if (!entry->module.empty())
			if (const auto it = byModule_.find(entry->module); it != byModule_.end()) {
				r.status = RegisterStatus::duplicate_module;
				r.message = "module " + entry->module + " already has an identity";
				r.identity = byId_.at(it->second);
				return r;
			}
		byId_.emplace(entry->descriptor.mod_id, entry);
		if (!entry->module.empty()) byModule_.emplace(entry->module, entry->descriptor.mod_id);
		generation_.fetch_add(1, std::memory_order_acq_rel);
		r.identity = std::move(entry);
		return r;
	}

	bool Registry::unregister_identity(const Uuid& mod_id)
	{
		std::unique_lock lock(lock_);
		const auto it = byId_.find(mod_id);
		if (it == byId_.end()) return false;
		if (!it->second->module.empty()) byModule_.erase(it->second->module);
		byId_.erase(it);
		generation_.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	bool Registry::unregister_module(std::string_view module)
	{
		std::unique_lock lock(lock_);
		const auto it = byModule_.find(std::string(module));
		if (it == byModule_.end()) return false;
		byId_.erase(it->second);
		byModule_.erase(it);
		generation_.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	void Registry::clear()
	{
		std::unique_lock lock(lock_);
		if (byId_.empty()) return;
		byId_.clear();
		byModule_.clear();
		generation_.fetch_add(1, std::memory_order_acq_rel);
	}

	IdentityPtr Registry::find_identity(const Uuid& mod_id) const
	{
		std::shared_lock lock(lock_);
		const auto it = byId_.find(mod_id);
		return it == byId_.end() ? nullptr : it->second;
	}

	IdentityPtr Registry::find_module(std::string_view module) const
	{
		std::shared_lock lock(lock_);
		const auto it = byModule_.find(std::string(module));
		return it == byModule_.end() ? nullptr : byId_.at(it->second);
	}

	IdentityPtr Registry::find_reverse_domain(std::string_view id) const
	{
		std::shared_lock lock(lock_);
		for (const auto& [key, entry] : byId_)
			if (entry->descriptor.reverse_domain == id) return entry;
		return nullptr;
	}

	std::vector<IdentityPtr> Registry::find_by_publisher(const Uuid& publisher_id) const
	{
		std::vector<IdentityPtr> out;
		std::shared_lock lock(lock_);
		for (const auto& [key, entry] : byId_)
			if (entry->descriptor.publisher_id == publisher_id) out.push_back(entry);
		return out;
	}

	std::vector<IdentityPtr> Registry::find_by_name(std::string_view name) const
	{
		std::vector<IdentityPtr> out;
		std::shared_lock lock(lock_);
		for (const auto& [key, entry] : byId_)
			if (entry->descriptor.name == name) out.push_back(entry);
		return out;
	}

	std::vector<IdentityPtr> Registry::snapshot() const
	{
		std::vector<IdentityPtr> out;
		std::shared_lock lock(lock_);
		out.reserve(byId_.size());
		for (const auto& [key, entry] : byId_) out.push_back(entry);
		return out;
	}

	std::size_t Registry::size() const
	{
		std::shared_lock lock(lock_);
		return byId_.size();
	}
}
