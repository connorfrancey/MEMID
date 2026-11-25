// The example binaries, built by this project with the real compiler and linker: the embedded identity and custom
// metadata are where the inspector looks, survive this build configuration's optimization, and match what the binary's
// own exported getter returns.

#include <memid/memid.hpp>

#include <gtest/gtest.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

using namespace memid;

TEST(MemidBinaries, ModDllCarriesItsIdentity)
{
	const Inspection i = inspect_file(MEMID_EXAMPLE_MOD);
	ASSERT_EQ(i.file_error, ErrorCode::none) << i.file_message;
	ASSERT_TRUE(i.descriptor) << i.identity_message;
	EXPECT_TRUE(i.identified());
	const Descriptor& d = *i.descriptor;
	EXPECT_EQ(d.mod_id.to_string(), "6f9619ff-8b86-d011-b42d-00c04fc964ff");
	EXPECT_EQ(d.publisher_id.to_string(), "a3bb189e-8bf9-3888-9912-ace4e6543002");
	EXPECT_EQ(d.reverse_domain, "com.example.minimal-mod");
	EXPECT_EQ(d.name, "Minimal Mod");
	EXPECT_EQ(d.publisher, "Example Publisher");
	EXPECT_EQ(d.version, (Version{1, 2, 3}));
	EXPECT_EQ(d.runtime, Runtime::mirage);
	EXPECT_EQ(d.game_id, game::mirrors_edge_2008_pc);
	EXPECT_EQ(d.capabilities, capability::gameplay | capability::native_hooks);
	EXPECT_EQ(d.protocol_flags, protocol::mirage_api | protocol::c_abi_getter);
	// Built for the machine the file says it is
	EXPECT_EQ(d.architecture, i.architecture);
	EXPECT_EQ(d.binary_format, i.format);
}

TEST(MemidBinaries, ModDllCarriesItsCustomMetadata)
{
	const Inspection i = inspect_file(MEMID_EXAMPLE_MOD);
	ASSERT_TRUE(i.has_custom_section);
	EXPECT_TRUE(i.custom.ok());
	const CustomMetadata& m = i.custom.metadata;
	ASSERT_EQ(m.size(), 3u);
	ASSERT_NE(m.find("com.example.minimal-mod"), nullptr);
	EXPECT_EQ(m.find("com.example.minimal-mod")->text(), "Hello from the minimal mod");
	ASSERT_NE(m.find("com.example.minimal-mod.blob"), nullptr);
	EXPECT_EQ(m.find("com.example.minimal-mod.blob")->payload, (std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0xFE, 0xFF}));
	const CustomEntry* json = m.find("com.example.minimal-mod.settings");
	ASSERT_NE(json, nullptr);
	EXPECT_EQ(json->format, static_cast<std::uint16_t>(MetadataFormat::json));
	EXPECT_NE(json->text().find("\"trail_color\": \"blue\""), std::string_view::npos);
}

TEST(MemidBinaries, IdentityFromCMakeHelpers)
{
	const Inspection i = inspect_file(MEMID_EXAMPLE_HELPER_MOD);
	ASSERT_TRUE(i.descriptor) << i.identity_message;
	EXPECT_TRUE(i.identified());
	EXPECT_EQ(i.descriptor->name, "Helper Mod");
	EXPECT_EQ(i.descriptor->version, (Version{2, 3, 4}));
	EXPECT_EQ(i.descriptor->channel, ReleaseChannel::beta);
	EXPECT_EQ(i.descriptor->runtime, Runtime::mirage);
	EXPECT_EQ(i.descriptor->interop_min, 1u);
	EXPECT_EQ(i.descriptor->interop_max, 3u);
	ASSERT_EQ(i.custom.metadata.size(), 1u);
	EXPECT_EQ(i.custom.metadata.entries()[0].text().substr(0, 8), "Built by");
}

TEST(MemidBinaries, ExecutableCarriesItsIdentity)
{
	const Inspection i = inspect_file(MEMID_EXAMPLE_TOOL);
	ASSERT_TRUE(i.descriptor) << i.identity_message;
	EXPECT_EQ(i.descriptor->name, "MEMID Example Tool");
	EXPECT_EQ(i.descriptor->game_id, game::none);
	EXPECT_FALSE(i.has_custom_section);
}

TEST(MemidBinaries, LegacyDllHasNoIdentity)
{
	const Inspection i = inspect_file(MEMID_EXAMPLE_LEGACY);
	EXPECT_EQ(i.file_error, ErrorCode::none);
	EXPECT_FALSE(i.has_identity_section);
	EXPECT_EQ(i.identity_error, ErrorCode::not_found);
	EXPECT_FALSE(i.has_custom_section);
}

// The getter MEMID_DEFINE_IDENTITY exports returns the same bytes the inspector found in the file
TEST(MemidBinaries, ExportedGetterMatchesTheSection)
{
	const Inspection i = inspect_file(MEMID_EXAMPLE_MOD);
	ASSERT_TRUE(i.descriptor);
	using Getter = const unsigned char*(MEMID_CALL*)(std::uint32_t*);
#if defined(_WIN32)
	HMODULE dll = LoadLibraryA(MEMID_EXAMPLE_MOD);
	ASSERT_NE(dll, nullptr);
	const auto get = reinterpret_cast<Getter>(GetProcAddress(dll, "memid_identity_descriptor"));
#else
	void* dll = dlopen(MEMID_EXAMPLE_MOD, RTLD_NOW);
	ASSERT_NE(dll, nullptr);
	const auto get = reinterpret_cast<Getter>(dlsym(dll, "memid_identity_descriptor"));
#endif
	ASSERT_NE(get, nullptr);
	std::uint32_t size = 0;
	const unsigned char* bytes = get(&size);
	EXPECT_EQ(size, kDescriptorSize);
	EXPECT_TRUE(std::equal(bytes, bytes + size, i.identity_bytes.begin(), i.identity_bytes.begin() + kDescriptorSize));
#if defined(_WIN32)
	FreeLibrary(dll);
#else
	dlclose(dll);
#endif
}
