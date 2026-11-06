#include "core/host_build.h"

#include <windows.h>
#include <wincrypt.h>

#include <array>

#include "core/log.h"

namespace {

constexpr std::array<unsigned char, 32> kOutfitStudio582Sha256 = {
	0x53, 0xCE, 0xC5, 0x95, 0x2A, 0x32, 0x0D, 0x2D,
	0x5E, 0xC9, 0x58, 0x2F, 0xE1, 0x11, 0xCC, 0xDB,
	0x73, 0x53, 0x04, 0xD2, 0xE9, 0x26, 0xE2, 0x72,
	0xCB, 0x65, 0x44, 0x0F, 0x4C, 0xC4, 0x21, 0x8C,
};

constexpr std::array<unsigned char, 32> kOutfitStudio591Sha256 = {
	0x7D, 0xB9, 0xE2, 0xFC, 0x7C, 0x9B, 0xB5, 0x5F,
	0x1A, 0xCB, 0x30, 0xC4, 0x77, 0x7C, 0x45, 0x7A,
	0xF2, 0x10, 0x7F, 0xA4, 0x57, 0xEE, 0x59, 0x40,
	0xF2, 0x23, 0x2B, 0xB2, 0xE1, 0xAA, 0x8E, 0x72,
};

constexpr std::array<unsigned char, 32> kOutfitStudio610Sha256 = {
	0xEA, 0xC1, 0xFA, 0x81, 0xCE, 0xEA, 0x98, 0x04,
	0x20, 0xDF, 0x59, 0x77, 0xE9, 0x39, 0x95, 0x20,
	0xDC, 0x71, 0x69, 0x34, 0xA7, 0x2A, 0x06, 0x08,
	0x49, 0xA5, 0xB3, 0xF7, 0x14, 0x8B, 0x20, 0x2C,
};

bool g_checked = false;
HostBuild::OutfitStudioBuild g_build = HostBuild::OutfitStudioBuild::Unsupported;

bool HashHost(std::array<unsigned char, 32>& digest) {
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
		return false;

	HANDLE file = CreateFileW(path, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
		CRYPT_VERIFYCONTEXT) != FALSE &&
		CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
	std::array<BYTE, 64 * 1024> buffer = {};
	while (ok) {
		DWORD read = 0;
		if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
			ok = false;
			break;
		}
		if (read == 0)
			break;
		ok = CryptHashData(hash, buffer.data(), read, 0) != FALSE;
	}
	if (ok) {
		DWORD bytes = static_cast<DWORD>(digest.size());
		ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &bytes, 0) != FALSE &&
			bytes == digest.size();
	}
	if (hash)
		CryptDestroyHash(hash);
	if (provider)
		CryptReleaseContext(provider, 0);
	CloseHandle(file);
	return ok;
}

} // namespace

namespace HostBuild {

OutfitStudioBuild CurrentOutfitStudio() {
	if (g_checked)
		return g_build;
	g_checked = true;
	std::array<unsigned char, 32> digest = {};
	if (HashHost(digest)) {
		if (digest == kOutfitStudio582Sha256)
			g_build = OutfitStudioBuild::V5_8_2;
		else if (digest == kOutfitStudio591Sha256)
			g_build = OutfitStudioBuild::V5_9_1;
		else if (digest == kOutfitStudio610Sha256)
			g_build = OutfitStudioBuild::V6_1_0;
	}
	const char* name = "NAO reconhecido";
	if (g_build == OutfitStudioBuild::V5_8_2)
		name = "5.8.2 reconhecido";
	else if (g_build == OutfitStudioBuild::V5_9_1)
		name = "5.9.1 reconhecido";
	else if (g_build == OutfitStudioBuild::V6_1_0)
		name = "6.1.0 reconhecido";
	LogF("host nativo: Outfit Studio %s", name);
	return g_build;
}

bool IsSupportedOutfitStudio() {
	return CurrentOutfitStudio() != OutfitStudioBuild::Unsupported;
}

bool IsSupportedOutfitStudio582() {
	return CurrentOutfitStudio() == OutfitStudioBuild::V5_8_2;
}

bool IsSupportedOutfitStudio591() {
	return CurrentOutfitStudio() == OutfitStudioBuild::V5_9_1;
}

bool IsSupportedOutfitStudio610() {
	return CurrentOutfitStudio() == OutfitStudioBuild::V6_1_0;
}

} // namespace HostBuild
