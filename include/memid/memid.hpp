// MEMID, the Mirror's Edge Mod Identifier: everything in one include.
#pragma once

#include <memid/binary_inspector.hpp>
#include <memid/custom_metadata.hpp>
#include <memid/descriptor.hpp>
#include <memid/diagnostics.hpp>
#include <memid/extensions.hpp>
#include <memid/format.hpp>
#include <memid/registry.hpp>
#include <memid/uuid.hpp>
#include <memid/version.hpp>

namespace memid
{
	// This library's version (the CMake project version)
	inline constexpr int kLibraryVersionMajor = 1, kLibraryVersionMinor = 1, kLibraryVersionPatch = 0;
}
