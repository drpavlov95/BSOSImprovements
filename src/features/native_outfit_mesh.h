#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "mesh/seams.h"

namespace NativeOutfitMesh {

enum class Result {
	Ok,
	UnsupportedBinary,
	NoSelectedMesh,
	InvalidLayout,
};

// Esta ponte e deliberadamente especifica para o Outfit Studio 5.8.2 cujo
// SHA-256 esta gravado no .cpp. Nada e lido ou escrito se o arquivo mudar.
bool SupportsRunningBinary();

struct Snapshot {
	SeamMesh mesh;
	uintptr_t nativeMesh = 0;
	uintptr_t nativeMask = 0;
};

Result SnapshotSelected(Snapshot& out);
Result ReplaceMask(const Snapshot& snapshot, const std::vector<uint16_t>& vertices,
				   HWND canvas);

} // namespace NativeOutfitMesh
