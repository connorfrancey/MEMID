// The identity registry: registration, duplicates, lookups, snapshots that outlive entries, and concurrent use.

#include <memid/memid.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <thread>

using namespace memid;

namespace
{
	Descriptor Mod(int n, const char* publisherId = "a3bb189e-8bf9-3888-9912-ace4e6543002")
	{
		char uuid[37];
		std::snprintf(uuid, sizeof uuid, "00000000-0000-4000-8000-%012d", n);
		return DescriptorBuilder{}.mod_id(uuid).publisher_id(publisherId).reverse_domain("com.example.mod" + std::to_string(n))
			.name("Mod " + std::to_string(n)).publisher("Example").build();
	}
}

TEST(MemidRegistry, RegisterFindUnregister)
{
	Registry r;
	const std::uint64_t g0 = r.generation();
	const RegisterResult a = r.register_identity(Mod(1), "mods/a.dll");
	ASSERT_TRUE(a) << a.message;
	EXPECT_EQ(r.size(), 1u);
	EXPECT_GT(r.generation(), g0);
	EXPECT_EQ(r.find_identity(Mod(1).mod_id), a.identity);
	EXPECT_EQ(r.find_module("mods/a.dll"), a.identity);
	EXPECT_EQ(r.find_reverse_domain("com.example.mod1"), a.identity);
	EXPECT_EQ(r.find_by_name("Mod 1").size(), 1u);
	EXPECT_EQ(r.find_by_publisher(*Uuid::parse("a3bb189e-8bf9-3888-9912-ace4e6543002")).size(), 1u);
	EXPECT_TRUE(r.unregister_identity(Mod(1).mod_id));
	EXPECT_FALSE(r.unregister_identity(Mod(1).mod_id));
	EXPECT_EQ(r.find_identity(Mod(1).mod_id), nullptr);
	EXPECT_EQ(r.find_module("mods/a.dll"), nullptr);
}

TEST(MemidRegistry, Duplicates)
{
	Registry r;
	ASSERT_TRUE(r.register_identity(Mod(1), "mods/a.dll"));
	const RegisterResult sameId = r.register_identity(Mod(1), "mods/b.dll");
	EXPECT_EQ(sameId.status, RegisterStatus::duplicate_mod_id);
	EXPECT_EQ(sameId.identity->module, "mods/a.dll");
	EXPECT_EQ(r.register_identity(Mod(2), "mods/a.dll").status, RegisterStatus::duplicate_module);
	EXPECT_TRUE(r.register_identity(Mod(2), ""));	// No module given: only the mod id must be unique
	EXPECT_TRUE(r.register_identity(Mod(3), ""));
	EXPECT_EQ(r.size(), 3u);
}

TEST(MemidRegistry, InvalidDescriptorIsRefused)
{
	Registry r;
	Descriptor d = Mod(1);
	d.mod_id = {};
	const RegisterResult res = r.register_identity(d, "x");
	EXPECT_EQ(res.status, RegisterStatus::invalid_descriptor);
	EXPECT_FALSE(res.message.empty());
	EXPECT_EQ(r.size(), 0u);
}

TEST(MemidRegistry, SnapshotsOutliveEntries)
{
	Registry r;
	r.register_identity(Mod(1), "a");
	r.register_identity(Mod(2), "b");
	const std::vector<IdentityPtr> snap = r.snapshot();
	const IdentityPtr one = r.find_identity(Mod(1).mod_id);
	r.clear();
	EXPECT_EQ(r.size(), 0u);
	EXPECT_EQ(snap.size(), 2u);
	EXPECT_EQ(one->descriptor.name, "Mod 1");	// Still valid: shared ownership
	EXPECT_TRUE(r.unregister_module("a") == false);
}

TEST(MemidRegistry, ConcurrentUse)
{
	Registry r;
	constexpr int kThreads = 8, kPerThread = 200;
	std::atomic<int> registered{0};
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; ++t)
		threads.emplace_back([&, t] {
			for (int i = 0; i < kPerThread; ++i) {
				const int n = t * kPerThread + i;
				if (r.register_identity(Mod(n), "m" + std::to_string(n))) ++registered;
				(void)r.find_identity(Mod(n).mod_id);
				(void)r.snapshot();
				if (i % 2) r.unregister_module("m" + std::to_string(n));
			}
		});
	for (auto& th : threads) th.join();
	EXPECT_EQ(registered.load(), kThreads * kPerThread);
	EXPECT_EQ(r.size(), static_cast<std::size_t>(kThreads * kPerThread / 2));
	for (const IdentityPtr& e : r.snapshot()) EXPECT_EQ(r.find_module(e->module), e);
}
