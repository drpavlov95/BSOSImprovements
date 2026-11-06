#pragma once

namespace HostBuild {

enum class OutfitStudioBuild {
	Unsupported,
	V5_8_2,
	V5_9_1,
	V6_1_0,
};

// Reconhece por SHA-256 a build exata do Outfit Studio em execucao. O
// resultado fica em cache depois da primeira chamada.
OutfitStudioBuild CurrentOutfitStudio();
bool IsSupportedOutfitStudio();
bool IsSupportedOutfitStudio582();
bool IsSupportedOutfitStudio591();
bool IsSupportedOutfitStudio610();

} // namespace HostBuild
