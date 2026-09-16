#include "features/native_outfit_mesh.h"

#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "core/log.h"

namespace {

// OutfitStudio.exe 5.8.2.0 usado para esta integracao.
constexpr std::array<unsigned char, 32> kSupportedSha256 = {
	0x53, 0xCE, 0xC5, 0x95, 0x2A, 0x32, 0x0D, 0x2D,
	0x5E, 0xC9, 0x58, 0x2F, 0xE1, 0x11, 0xCC, 0xDB,
	0x73, 0x53, 0x04, 0xD2, 0xE9, 0x26, 0xE2, 0x72,
	0xCB, 0x65, 0x44, 0x0F, 0x4C, 0xC4, 0x21, 0x8C,
};

// RTTI/vftable de wxGLPanel nesta build. ASLR e aplicado somando a base viva.
constexpr uintptr_t kWxGlPanelVtableRva = 0x199F2F8;

// Layout de Mesh nesta mesma build (MSVC x64). Antes de qualquer escrita todos
// os ponteiros, contagens, triangulos e valores amostrados sao validados.
constexpr size_t kMeshQueueMaskOffset = 7; // Mesh::UpdateType::Mask
constexpr size_t kMeshVertexCountOffset = 180;
constexpr size_t kMeshVerticesOffset = 184;
constexpr size_t kMeshUvsOffset = 232;
constexpr size_t kMeshMaskOffset = 240;
constexpr size_t kMeshTrianglesOffset = 256;
constexpr size_t kMeshTriangleCountOffset = 272;

struct NativeVec3 { float x, y, z; };
struct NativeVec2 { float u, v; };
struct NativeTriangle { uint16_t a, b, c; };

bool g_supportChecked = false;
bool g_supported = false;
bool g_panelScanComplete = false;
std::vector<uintptr_t> g_panels;

DWORD BasicProtection(DWORD protection) {
	return protection & 0xFFu;
}

bool ProtectionReadable(DWORD protection) {
	if (protection & (PAGE_GUARD | PAGE_NOACCESS))
		return false;
	switch (BasicProtection(protection)) {
		case PAGE_READONLY:
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
	}
}

bool ProtectionWritable(DWORD protection) {
	if (protection & (PAGE_GUARD | PAGE_NOACCESS))
		return false;
	switch (BasicProtection(protection)) {
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
	}
}

bool RangeHasProtection(uintptr_t address, size_t size, bool writable) {
	if (!address || size == 0 || address > (std::numeric_limits<uintptr_t>::max)() - size)
		return false;
	const uintptr_t end = address + size;
	uintptr_t cursor = address;
	while (cursor < end) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) ||
			mbi.State != MEM_COMMIT ||
			!(writable ? ProtectionWritable(mbi.Protect) : ProtectionReadable(mbi.Protect)))
			return false;
		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (regionEnd <= cursor)
			return false;
		cursor = (std::min)(end, regionEnd);
	}
	return true;
}

template<typename T>
bool ReadValue(uintptr_t address, T& value) {
	if (!RangeHasProtection(address, sizeof(T), false))
		return false;
	SIZE_T read = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
							 &value, sizeof(T), &read) != FALSE && read == sizeof(T);
}

bool HashHost(std::array<unsigned char, 32>& digest) {
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
		return false;

	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
						  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE &&
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

std::vector<uintptr_t> FindObjectsWithVtable(uintptr_t vtable) {
	std::vector<uintptr_t> found;
	std::vector<unsigned char> copy(1024 * 1024);
	SYSTEM_INFO info = {};
	GetSystemInfo(&info);
	uintptr_t cursor = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
	const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
	while (cursor < maximum) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)))
			break;
		const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		const uintptr_t next = base + mbi.RegionSize;
		if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && ProtectionReadable(mbi.Protect)) {
			for (uintptr_t chunk = base; chunk < next;) {
				const size_t wanted = static_cast<size_t>((std::min)(
					static_cast<uintptr_t>(copy.size()), next - chunk));
				SIZE_T read = 0;
				if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(chunk),
								  copy.data(), wanted, &read) && read >= sizeof(uintptr_t)) {
					const size_t first = static_cast<size_t>((sizeof(uintptr_t) -
						(chunk & (sizeof(uintptr_t) - 1))) & (sizeof(uintptr_t) - 1));
					for (size_t offset = first; offset + sizeof(uintptr_t) <= read;
						 offset += sizeof(uintptr_t)) {
						uintptr_t value = 0;
						std::memcpy(&value, copy.data() + offset, sizeof(value));
						if (value == vtable)
							found.push_back(chunk + offset);
					}
				}
				if (wanted == 0)
					break;
				chunk += wanted;
			}
		}
		if (next <= cursor)
			break;
		cursor = next;
	}
	return found;
}

bool ReadMeshLayout(uintptr_t mesh, int& vertexCount, uintptr_t& vertices,
					uintptr_t& uvs, uintptr_t& mask, uintptr_t& triangles, int& triangleCount) {
	if (!RangeHasProtection(mesh, kMeshTriangleCountOffset + sizeof(int), false))
		return false;
	if (!ReadValue(mesh + kMeshVertexCountOffset, vertexCount) ||
		!ReadValue(mesh + kMeshVerticesOffset, vertices) ||
		!ReadValue(mesh + kMeshUvsOffset, uvs) ||
		!ReadValue(mesh + kMeshMaskOffset, mask) ||
		!ReadValue(mesh + kMeshTrianglesOffset, triangles) ||
		!ReadValue(mesh + kMeshTriangleCountOffset, triangleCount))
		return false;
	if (vertexCount <= 0 || vertexCount > 65535 || triangleCount <= 0 || triangleCount > 500000)
		return false;
	if (!RangeHasProtection(vertices, static_cast<size_t>(vertexCount) * sizeof(NativeVec3), false) ||
		!RangeHasProtection(mask, static_cast<size_t>(vertexCount) * sizeof(float), true) ||
		!RangeHasProtection(triangles, static_cast<size_t>(triangleCount) * sizeof(NativeTriangle), false))
		return false;
	if (uvs && !RangeHasProtection(uvs, static_cast<size_t>(vertexCount) * sizeof(NativeVec2), false))
		return false;

	// Uma amostra pequena elimina ponteiros que por acaso imitam as contagens.
	for (int i : {0, vertexCount / 2, vertexCount - 1}) {
		NativeVec3 value = {};
		float maskValue = 0.0f;
		if (!ReadValue(vertices + static_cast<size_t>(i) * sizeof(value), value) ||
			!ReadValue(mask + static_cast<size_t>(i) * sizeof(maskValue), maskValue) ||
			!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
			!std::isfinite(maskValue) || maskValue < 0.0f || maskValue > 1.0f)
			return false;
	}
	for (int i : {0, triangleCount / 2, triangleCount - 1}) {
		NativeTriangle tri = {};
		if (!ReadValue(triangles + static_cast<size_t>(i) * sizeof(tri), tri) ||
			tri.a >= vertexCount || tri.b >= vertexCount || tri.c >= vertexCount)
			return false;
	}
	return true;
}

bool IsMeshInVector(uintptr_t begin, uintptr_t end, uintptr_t mesh) {
	for (uintptr_t at = begin; at < end; at += sizeof(uintptr_t)) {
		uintptr_t item = 0;
		if (!ReadValue(at, item))
			return false;
		if (item == mesh)
			return true;
	}
	return false;
}

uintptr_t FindSelectedMeshInPanel(uintptr_t panel) {
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(panel), &mbi, sizeof(mbi)) ||
		mbi.State != MEM_COMMIT || !ProtectionReadable(mbi.Protect))
		return 0;
	const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	const size_t available = static_cast<size_t>(regionEnd - panel);
	const size_t scanBytes = std::min<size_t>(available, 0x10000);
	std::vector<unsigned char> object(scanBytes);
	SIZE_T copied = 0;
	if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(panel),
						   object.data(), object.size(), &copied) || copied < 4 * sizeof(uintptr_t))
		return 0;

	for (size_t offset = 0; offset + 4 * sizeof(uintptr_t) <= copied; offset += sizeof(uintptr_t)) {
		uintptr_t begin = 0, end = 0, capacity = 0, selected = 0;
		std::memcpy(&begin, object.data() + offset, sizeof(begin));
		std::memcpy(&end, object.data() + offset + 8, sizeof(end));
		std::memcpy(&capacity, object.data() + offset + 16, sizeof(capacity));
		std::memcpy(&selected, object.data() + offset + 24, sizeof(selected));
		if (!begin || begin > end || end > capacity || (end - begin) % sizeof(uintptr_t) != 0)
			continue;
		const size_t count = static_cast<size_t>((end - begin) / sizeof(uintptr_t));
		if (count == 0 || count > 512 || !selected ||
			!RangeHasProtection(begin, count * sizeof(uintptr_t), false) ||
			!IsMeshInVector(begin, end, selected))
			continue;

		int nv = 0, nt = 0;
		uintptr_t v = 0, uv = 0, m = 0, t = 0;
		if (!ReadMeshLayout(selected, nv, v, uv, m, t, nt))
			continue;
		return selected;
	}
	return 0;
}

uintptr_t FindSelectedMesh() {
	HMODULE host = GetModuleHandleW(nullptr);
	if (!host)
		return 0;
	const uintptr_t vtable = reinterpret_cast<uintptr_t>(host) + kWxGlPanelVtableRva;
	if (!g_panelScanComplete) {
		g_panels = FindObjectsWithVtable(vtable);
		g_panelScanComplete = true;
		LogF("seams nativo: %zu wxGLPanel localizado(s)", g_panels.size());
	}
	for (uintptr_t panel : g_panels) {
		uintptr_t actualVtable = 0;
		if (!ReadValue(panel, actualVtable) || actualVtable != vtable)
			continue;
		if (uintptr_t mesh = FindSelectedMeshInPanel(panel))
			return mesh;
	}
	return 0;
}

} // namespace

namespace NativeOutfitMesh {

bool SupportsRunningBinary() {
	if (g_supportChecked)
		return g_supported;
	g_supportChecked = true;
	std::array<unsigned char, 32> digest = {};
	g_supported = HashHost(digest) && digest == kSupportedSha256;
	LogF("seams nativo: binario 5.8.2 %s", g_supported ? "reconhecido" : "NAO reconhecido");
	return g_supported;
}

Result SnapshotSelected(Snapshot& out) {
	out = Snapshot();
	if (!SupportsRunningBinary())
		return Result::UnsupportedBinary;
	const uintptr_t mesh = FindSelectedMesh();
	if (!mesh)
		return Result::NoSelectedMesh;

	int vertexCount = 0, triangleCount = 0;
	uintptr_t vertices = 0, uvs = 0, mask = 0, triangles = 0;
	if (!ReadMeshLayout(mesh, vertexCount, vertices, uvs, mask, triangles, triangleCount))
		return Result::InvalidLayout;

	out.mesh.vertices.resize(static_cast<size_t>(vertexCount));
	out.mesh.triangles.resize(static_cast<size_t>(triangleCount));
	for (int i = 0; i < vertexCount; ++i) {
		NativeVec3 pos = {};
		std::memcpy(&pos, reinterpret_cast<const void*>(vertices + static_cast<size_t>(i) * sizeof(pos)), sizeof(pos));
		SeamVertex& vertex = out.mesh.vertices[static_cast<size_t>(i)];
		vertex.x = pos.x;
		vertex.y = pos.y;
		vertex.z = pos.z;
		if (uvs) {
			NativeVec2 uv = {};
			std::memcpy(&uv, reinterpret_cast<const void*>(uvs + static_cast<size_t>(i) * sizeof(uv)), sizeof(uv));
			vertex.u = uv.u;
			vertex.v = uv.v;
			vertex.hasUv = std::isfinite(uv.u) && std::isfinite(uv.v);
		}
	}
	for (int i = 0; i < triangleCount; ++i) {
		NativeTriangle tri = {};
		std::memcpy(&tri, reinterpret_cast<const void*>(triangles + static_cast<size_t>(i) * sizeof(tri)), sizeof(tri));
		out.mesh.triangles[static_cast<size_t>(i)] = {tri.a, tri.b, tri.c};
	}
	out.nativeMesh = mesh;
	out.nativeMask = mask;
	return Result::Ok;
}

Result ReplaceMask(const Snapshot& snapshot, const std::vector<uint16_t>& vertices, HWND canvas) {
	if (!SupportsRunningBinary())
		return Result::UnsupportedBinary;
	if (!snapshot.nativeMesh || !snapshot.nativeMask || snapshot.mesh.vertices.empty())
		return Result::InvalidLayout;

	int vertexCount = 0, triangleCount = 0;
	uintptr_t nativeVertices = 0, nativeUvs = 0, mask = 0, triangles = 0;
	if (!ReadMeshLayout(snapshot.nativeMesh, vertexCount, nativeVertices, nativeUvs, mask, triangles,
						triangleCount) ||
		vertexCount != static_cast<int>(snapshot.mesh.vertices.size()) || mask != snapshot.nativeMask)
		return Result::InvalidLayout;

	std::fill_n(reinterpret_cast<float*>(mask), vertexCount, 0.0f);
	for (uint16_t index : vertices)
		if (index < vertexCount)
			reinterpret_cast<float*>(mask)[index] = 1.0f;

	// Equivale a Mesh::QueueUpdate(Mask), cujo armazenamento e um array de bool
	// no inicio do objeto. O paint nativo envia o novo buffer para OpenGL.
	reinterpret_cast<unsigned char*>(snapshot.nativeMesh)[kMeshQueueMaskOffset] = 1;
	if (canvas && IsWindow(canvas))
		RedrawWindow(canvas, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
	return Result::Ok;
}

} // namespace NativeOutfitMesh
