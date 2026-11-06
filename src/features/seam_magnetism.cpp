#include "features/seam_magnetism.h"

#include <windows.h>

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/host_build.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "mesh/contact.h"

namespace {

// RVAs medidos nas builds exatas reconhecidas por SHA-256. Cada endereco e
// conferido contra a desmontagem antes de qualquer escrita.
struct NativeProfile {
	uintptr_t updateStrokeThunkRva;
	uintptr_t updateStrokeBodyRva;
	uintptr_t tryEmplaceThunkRva;
	uintptr_t tryEmplaceBodyRva;
	std::array<uintptr_t, 4> updateStrokeCallRvas;
	size_t undoShapeStride;
	size_t meshVertexCountOffset;
	size_t meshVerticesOffset;
	size_t meshTrianglesOffset;
	size_t meshTriangleCountOffset;
};

constexpr NativeProfile kProfile582 = {
	0x02E7B2, 0x611F40, 0x00C0E0, 0x5E2A20,
	{0x898D98, 0x89FBEB, 0x8A41C3, 0x8A6BA6},
	0x290, 180, 184, 256, 272
};

constexpr NativeProfile kProfile591 = {
	0x032934, 0x705580, 0x00D13E, 0x6D6100,
	{0x97D0B8, 0x984C1B, 0x98A323, 0x98CF16},
	0x290, 180, 184, 256, 272
};

// 6.1 introduced extra Mesh fields and two cloth-source fields in the undo
// record. The helper is the int -> Vector3 unordered_map instantiation used
// by UndoStateShape; all addresses are checked again before patching.
constexpr NativeProfile kProfile610 = {
	0x03B0AC, 0x837D90, 0x00F263, 0x80C150,
	{0xB263E3, 0xB29A66, 0xB7BFB3, 0xB7EDF2},
	0x2C0, 180, 184, 272, 288
};

const NativeProfile* RunningProfile() {
	switch (HostBuild::CurrentOutfitStudio()) {
		case HostBuild::OutfitStudioBuild::V5_8_2: return &kProfile582;
		case HostBuild::OutfitStudioBuild::V5_9_1: return &kProfile591;
		case HostBuild::OutfitStudioBuild::V6_1_0: return &kProfile610;
		default: return nullptr;
	}
}

// Prologo de updateStroke. O primeiro `mov [rsp+0x10], rdx` e o que salva o
// pickInfo antes de empurrar os registradores preservados.
constexpr std::array<unsigned char, 22> kUpdateStrokePrologue = {
	0x48, 0x89, 0x54, 0x24, 0x10,             // mov [rsp+10h], rdx
	0x48, 0x89, 0x4C, 0x24, 0x08,             // mov [rsp+08h], rcx
	0x55, 0x53, 0x56, 0x57,                   // push rbp, rbx, rsi, rdi
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 // push r12..r15
};

constexpr std::array<unsigned char, 17> kTryEmplacePrologue = {
	0x40, 0x53, 0x55, 0x56, 0x57,             // push rbx, rbp, rsi, rdi
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, // push r12..r15
	0x48, 0x83, 0xEC, 0x38                    // sub rsp, 38h
};

// Campos de TweakStroke.
constexpr size_t kStrokeMeshesBegin = 0x00;
constexpr size_t kStrokeMeshesEnd = 0x08;
constexpr size_t kStrokeFirstUpdate = 0x20; // byte; updateStroke zera na primeira vez
constexpr size_t kStrokeUndoProject = 0xF0;

// UndoStateProject guarda um vector<UndoStateShape> em +0x08, na mesma ordem do
// vetor de meshes do traco. O passo de 0x290 saiu da aritmetica que o proprio
// updateStroke faz para achar o estado de cada mesh.
constexpr size_t kUndoShapesBegin = 0x08;
constexpr size_t kUndoShapesEnd = 0x10;
constexpr size_t kShapeStartMap = 0x20; // posicao de partida de cada vertice
constexpr size_t kShapeEndMap = 0x60;   // posicao final

// std::unordered_map da MSVC: sentinela da lista em +0x08 e tamanho em +0x10.
// No node, _Next em +0, _Prev em +8, a chave int em +0x10 e o Vector3 em +0x14.
constexpr size_t kMapHead = 0x08;
constexpr size_t kMapSize = 0x10;
constexpr size_t kNodeNext = 0x00;
constexpr size_t kNodeKey = 0x10;
constexpr size_t kNodeValue = 0x14;

// Campos de Mesh, os mesmos que native_outfit_mesh.cpp ja usa nesta build.
constexpr size_t kMeshQueuePositionOffset = 0; // Mesh::UpdateType::Position

// Teto de seguranca ao andar na lista do mapa do host: uma lista corrompida nao
// pode virar laco infinito dentro do traco.
constexpr size_t kMaxMapWalk = 4u * 1000u * 1000u;

struct NativeVec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct NativeTriangle {
	uint16_t a = 0;
	uint16_t b = 0;
	uint16_t c = 0;
};

using UpdateStroke = void(__fastcall*)(void*, void*);
using TryEmplace = void*(__fastcall*)(void*, void*, const int*);

struct CallPatch {
	uintptr_t address = 0;
	std::array<unsigned char, 5> original = {};
	bool installed = false;
};

// Uma mesh do grafo congelado. Os triangulos sao copiados porque o host os
// guarda como uint16 e o solver fala em int; as posicoes nunca sao copiadas --
// o solver le o array vivo, que e o ponto de estar dentro do traco.
struct FrozenMesh {
	uintptr_t nativeMesh = 0;
	int vertexCount = 0;
	std::vector<int> triangles;
};

bool g_enabled = false;
UpdateStroke g_originalUpdateStroke = nullptr;
float g_tolerance = 0.0f;
int g_falloffRings = 1;
bool g_graphFrozen = false;
ContactGraph g_graph;
std::vector<FrozenMesh> g_frozen;
std::unordered_map<uint64_t, NativeVec3> g_written;
std::array<CallPatch, 4> g_patches;
void* g_hookRelay = nullptr;
const NativeProfile* g_profile = nullptr;

const NativeProfile* ActiveProfile() {
	return g_profile ? g_profile : RunningProfile();
}

uint64_t VertexKey(int mesh, int vertex) {
	return (static_cast<uint64_t>(static_cast<uint32_t>(mesh)) << 32) |
		   static_cast<uint32_t>(vertex);
}

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

bool RangeReadable(uintptr_t address, size_t size) {
	if (!address || !size || address > (std::numeric_limits<uintptr_t>::max)() - size)
		return false;
	const uintptr_t end = address + size;
	for (uintptr_t cursor = address; cursor < end;) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) ||
			mbi.State != MEM_COMMIT || !ProtectionReadable(mbi.Protect))
			return false;
		const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (next <= cursor)
			return false;
		cursor = (std::min)(end, next);
	}
	return true;
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

bool RangeWritable(uintptr_t address, size_t size) {
	if (!address || !size || address > (std::numeric_limits<uintptr_t>::max)() - size)
		return false;
	const uintptr_t end = address + size;
	for (uintptr_t cursor = address; cursor < end;) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) ||
			mbi.State != MEM_COMMIT || !ProtectionWritable(mbi.Protect))
			return false;
		const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (next <= cursor)
			return false;
		cursor = (std::min)(end, next);
	}
	return true;
}

bool ReadPointer(uintptr_t address, uintptr_t& value) {
	if (!RangeReadable(address, sizeof(value)))
		return false;
	std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
	return true;
}

bool ReadInt(uintptr_t address, int& value) {
	if (!RangeReadable(address, sizeof(value)))
		return false;
	std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
	return true;
}

bool WriteCode(uintptr_t address, const void* bytes, size_t size) {
	DWORD oldProtection = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE,
						&oldProtection))
		return false;
	std::memcpy(reinterpret_cast<void*>(address), bytes, size);
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
	DWORD ignored = 0;
	VirtualProtect(reinterpret_cast<void*>(address), size, oldProtection, &ignored);
	return true;
}

// Um `call rel32` so alcanca +/- 2 GiB. O executavel fica perto de 0x140000000,
// mas o loader pode por este DLL em 0x7FF..., longe demais. Reserva uma pagina
// no intervalo comum aos quatro call sites e grava nela um salto absoluto que
// nao altera registrador nenhum: `jmp qword ptr [rip+0] ; dq destino`.
uintptr_t CreateRelayNear(const std::array<uintptr_t, 4>& sites, uintptr_t target) {
	int64_t lowest = 0;
	int64_t highest = (std::numeric_limits<int64_t>::max)();
	for (uintptr_t site : sites) {
		const int64_t next = static_cast<int64_t>(site) + 5;
		lowest = (std::max)(lowest, next + (std::numeric_limits<int32_t>::min)());
		highest = (std::min)(highest, next + (std::numeric_limits<int32_t>::max)());
	}
	if (lowest > highest)
		return 0;

	SYSTEM_INFO system = {};
	GetSystemInfo(&system);
	const uintptr_t granularity = system.dwAllocationGranularity;
	const size_t pageSize = system.dwPageSize;
	uintptr_t cursor = static_cast<uintptr_t>((std::max<int64_t>)(lowest, 0x10000));
	const uintptr_t last = static_cast<uintptr_t>(highest);

	while (cursor <= last) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)))
			break;
		const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		const uintptr_t regionEnd = region + mbi.RegionSize;
		if (mbi.State == MEM_FREE) {
			const uintptr_t unaligned = (std::max)(cursor, region);
			const uintptr_t candidate =
				(unaligned + granularity - 1) & ~(granularity - 1);
			if (candidate <= last && candidate <= regionEnd &&
				pageSize <= regionEnd - candidate) {
				void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), pageSize,
										MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
				if (memory) {
					unsigned char relay[14] = {0xFF, 0x25, 0, 0, 0, 0};
					std::memcpy(relay + 6, &target, sizeof(target));
					std::memcpy(memory, relay, sizeof(relay));
					DWORD oldProtection = 0;
					if (VirtualProtect(memory, pageSize, PAGE_EXECUTE_READ, &oldProtection)) {
						FlushInstructionCache(GetCurrentProcess(), memory, sizeof(relay));
						return reinterpret_cast<uintptr_t>(memory);
					}
					VirtualFree(memory, 0, MEM_RELEASE);
				}
			}
		}
		if (regionEnd <= cursor)
			break;
		cursor = regionEnd;
	}
	return 0;
}

void ReleaseRelay() {
	if (g_hookRelay)
		VirtualFree(g_hookRelay, 0, MEM_RELEASE);
	g_hookRelay = nullptr;
}

uintptr_t HostBase() {
	HMODULE host = GetModuleHandleW(nullptr);
	return host ? reinterpret_cast<uintptr_t>(host) : 0;
}

// Le o layout de uma Mesh do host, conferindo que contagens e ponteiros fazem
// sentido antes de qualquer uso.
bool ReadMeshLayout(uintptr_t mesh, int& vertexCount, uintptr_t& vertices, uintptr_t& triangles,
					int& triangleCount) {
	const NativeProfile* profile = ActiveProfile();
	if (!profile || !RangeReadable(mesh, profile->meshTriangleCountOffset + sizeof(int)))
		return false;
	if (!ReadInt(mesh + profile->meshVertexCountOffset, vertexCount) ||
		!ReadInt(mesh + profile->meshTriangleCountOffset, triangleCount) ||
		!ReadPointer(mesh + profile->meshVerticesOffset, vertices) ||
		!ReadPointer(mesh + profile->meshTrianglesOffset, triangles))
		return false;
	if (vertexCount <= 0 || vertexCount > 0xFFFF || triangleCount < 0 ||
		triangleCount > 4 * 1000 * 1000)
		return false;
	if (!RangeReadable(vertices, static_cast<size_t>(vertexCount) * sizeof(NativeVec3)))
		return false;
	if (triangleCount > 0 &&
		!RangeReadable(triangles, static_cast<size_t>(triangleCount) * sizeof(NativeTriangle)))
		return false;
	return true;
}

// As meshes deste traco, na ordem em que o host as guarda -- a mesma ordem dos
// UndoStateShape.
bool ReadStrokeMeshes(uintptr_t stroke, std::vector<uintptr_t>& meshes) {
	meshes.clear();
	uintptr_t begin = 0;
	uintptr_t end = 0;
	if (!ReadPointer(stroke + kStrokeMeshesBegin, begin) ||
		!ReadPointer(stroke + kStrokeMeshesEnd, end) || begin == 0 || end < begin)
		return false;
	const size_t bytes = static_cast<size_t>(end - begin);
	if (bytes % sizeof(uintptr_t) != 0 || bytes > 4096 * sizeof(uintptr_t))
		return false;
	if (!RangeReadable(begin, bytes))
		return false;
	for (uintptr_t cursor = begin; cursor < end; cursor += sizeof(uintptr_t)) {
		uintptr_t mesh = 0;
		std::memcpy(&mesh, reinterpret_cast<const void*>(cursor), sizeof(mesh));
		if (!mesh)
			return false;
		meshes.push_back(mesh);
	}
	return !meshes.empty();
}

// O vetor de UndoStateShape, conferido contra a contagem de meshes. Se os dois
// discordarem, o layout nao e o que medimos e nada deve ser escrito.
bool ReadUndoShapes(uintptr_t stroke, size_t meshCount, uintptr_t& shapesBegin) {
	shapesBegin = 0;
	uintptr_t project = 0;
	if (!ReadPointer(stroke + kStrokeUndoProject, project) || !project)
		return false;
	uintptr_t end = 0;
	if (!ReadPointer(project + kUndoShapesBegin, shapesBegin) ||
		!ReadPointer(project + kUndoShapesEnd, end) || !shapesBegin || end < shapesBegin)
		return false;
	const size_t bytes = static_cast<size_t>(end - shapesBegin);
	const NativeProfile* profile = ActiveProfile();
	if (!profile || bytes % profile->undoShapeStride != 0 ||
		bytes / profile->undoShapeStride != meshCount)
		return false;
	return RangeReadable(shapesBegin, bytes);
}

// Percorre o mapa de fim de um UndoStateShape, entregando indice e posicao de
// cada vertice que o traco ja mexeu.
template <typename Visitor>
bool WalkMap(uintptr_t map, Visitor visit) {
	uintptr_t head = 0;
	uintptr_t size = 0;
	if (!ReadPointer(map + kMapHead, head) || !head ||
		!ReadPointer(map + kMapSize, size) || size > kMaxMapWalk)
		return false;
	uintptr_t node = 0;
	if (!ReadPointer(head + kNodeNext, node))
		return false;
	for (size_t visited = 0; node && node != head; ++visited) {
		if (visited > size || !RangeReadable(node, kNodeValue + sizeof(NativeVec3)))
			return false;
		int key = 0;
		NativeVec3 value;
		std::memcpy(&key, reinterpret_cast<const void*>(node + kNodeKey), sizeof(key));
		std::memcpy(&value, reinterpret_cast<const void*>(node + kNodeValue), sizeof(value));
		visit(key, value);
		if (!ReadPointer(node + kNodeNext, node))
			return false;
	}
	return true;
}

// Escreve um vertice no mapa do host chamando o try_emplace do proprio host, em
// vez de reimplementar a tabela hash da MSVC. Devolve false se o no nao veio.
bool PutInMap(uintptr_t map, int vertex, const NativeVec3& position, bool onlyIfAbsent) {
	const uintptr_t base = HostBase();
	if (!base)
		return false;
	alignas(8) unsigned char result[32] = {};
	const int key = vertex;
	if (!g_profile)
		return false;
	reinterpret_cast<TryEmplace>(base + g_profile->tryEmplaceThunkRva)(reinterpret_cast<void*>(map), result,
											&key);

	uintptr_t node = 0;
	std::memcpy(&node, result, sizeof(node));
	const bool inserted = result[sizeof(node)] != 0;
	if (!node || !RangeReadable(node, kNodeValue + sizeof(NativeVec3)))
		return false;
	if (onlyIfAbsent && !inserted)
		return true; // o pincel ja gravou a partida deste vertice; nao e nossa
	std::memcpy(reinterpret_cast<void*>(node + kNodeValue), &position, sizeof(position));
	return true;
}

// Monta o grafo a partir das meshes deste traco, e congela. Chamado uma vez,
// no primeiro traco depois de o usuario ligar o magnetismo.
bool FreezeGraph(const std::vector<uintptr_t>& meshes) {
	g_frozen.clear();
	std::vector<ContactMesh> inputs;
	std::vector<std::vector<int>> storage;
	storage.reserve(meshes.size());

	for (uintptr_t mesh : meshes) {
		int vertexCount = 0;
		int triangleCount = 0;
		uintptr_t vertices = 0;
		uintptr_t triangles = 0;
		if (!ReadMeshLayout(mesh, vertexCount, vertices, triangles, triangleCount)) {
			LogF("magnetismo: layout de Mesh nao confere; grafo nao foi montado");
			g_frozen.clear();
			return false;
		}
		FrozenMesh frozen;
		frozen.nativeMesh = mesh;
		frozen.vertexCount = vertexCount;
		frozen.triangles.reserve(static_cast<size_t>(triangleCount) * 3);
		const NativeTriangle* source = reinterpret_cast<const NativeTriangle*>(triangles);
		for (int i = 0; i < triangleCount; ++i) {
			frozen.triangles.push_back(source[i].a);
			frozen.triangles.push_back(source[i].b);
			frozen.triangles.push_back(source[i].c);
		}
		g_frozen.push_back(std::move(frozen));
	}

	inputs.reserve(g_frozen.size());
	for (const FrozenMesh& frozen : g_frozen) {
		uintptr_t vertices = 0;
		const NativeProfile* profile = ActiveProfile();
		if (!profile || !ReadPointer(frozen.nativeMesh + profile->meshVerticesOffset, vertices))
			continue;
		ContactMesh input;
		input.positions = reinterpret_cast<const Vec3*>(vertices);
		input.vertexCount = static_cast<size_t>(frozen.vertexCount);
		input.triangles = frozen.triangles.data();
		input.triangleCount = frozen.triangles.size() / 3;
		inputs.push_back(input);
	}

	g_graph = BuildContactGraph(inputs, g_tolerance);
	g_graphFrozen = true;
	LogF("magnetismo: grafo congelado com %zu mesh(es), %zu cluster(es) e %zu vertice(s) em aresta",
		 g_frozen.size(), g_graph.clusters.size(), g_graph.onEdges.size());
	return true;
}

// Liga as meshes do grafo as deste traco, por ponteiro. Uma mesh do grafo que
// nao esteja neste traco entra como nula, e o solver a ignora sozinho.
std::vector<ContactMesh> BindMeshes(const std::vector<uintptr_t>& strokeMeshes,
									std::vector<int>& strokeIndexOfGraphMesh) {
	std::vector<ContactMesh> bound(g_frozen.size());
	strokeIndexOfGraphMesh.assign(g_frozen.size(), -1);
	for (size_t graphIndex = 0; graphIndex < g_frozen.size(); ++graphIndex) {
		const FrozenMesh& frozen = g_frozen[graphIndex];
		const auto found = std::find(strokeMeshes.begin(), strokeMeshes.end(), frozen.nativeMesh);
		if (found == strokeMeshes.end())
			continue;
		int vertexCount = 0;
		int triangleCount = 0;
		uintptr_t vertices = 0;
		uintptr_t triangles = 0;
		if (!ReadMeshLayout(frozen.nativeMesh, vertexCount, vertices, triangles, triangleCount) ||
			vertexCount != frozen.vertexCount)
			continue; // a mesh mudou de tamanho: os indices do grafo nao valem mais
		// Vamos escrever neste array; conferir agora evita descobrir no meio da
		// correcao, com metade dela aplicada.
		if (!RangeWritable(vertices, static_cast<size_t>(vertexCount) * sizeof(NativeVec3)))
			continue;
		strokeIndexOfGraphMesh[graphIndex] =
			static_cast<int>(std::distance(strokeMeshes.begin(), found));
		bound[graphIndex].positions = reinterpret_cast<const Vec3*>(vertices);
		bound[graphIndex].vertexCount = static_cast<size_t>(vertexCount);
		bound[graphIndex].triangles = frozen.triangles.data();
		bound[graphIndex].triangleCount = frozen.triangles.size() / 3;
	}
	return bound;
}

// O que o PINCEL mexeu neste traco. O mapa de fim tambem carrega o que nos
// escrevemos; comparar com o que gravamos separa uma coisa da outra, e e o que
// impede o lado que segue de virar o lado que manda.
//
// Devolve false quando o mapa nao esta guardando posicao de vertice. Um pincel
// de mascara, de peso ou de cor tambem escreve nos mapas do UndoStateShape, e
// tratar um peso como se fosse posicao deformaria a malha. A invariante que
// separa os dois e barata e exata: num pincel de geometria, o que esta no mapa
// de fim e igual ao que esta no array de vertices, porque brushAction escreve
// os dois com o mesmo valor.
bool CollectBrushMoved(const std::vector<ContactMesh>& bound,
					   const std::vector<int>& strokeIndexOfGraphMesh, uintptr_t shapesBegin,
					   std::vector<ContactVertex>& moved) {
	moved.clear();
	bool positionsMatch = true;
	for (size_t graphIndex = 0; graphIndex < bound.size(); ++graphIndex) {
		const int strokeIndex = strokeIndexOfGraphMesh[graphIndex];
		if (strokeIndex < 0 || !bound[graphIndex].positions)
			continue;
		const uintptr_t shape =
			shapesBegin + static_cast<size_t>(strokeIndex) * ActiveProfile()->undoShapeStride;
		const size_t vertexCount = bound[graphIndex].vertexCount;
		const NativeVec3* live = reinterpret_cast<const NativeVec3*>(bound[graphIndex].positions);
		const bool walked =
			WalkMap(shape + kShapeEndMap, [&](int vertex, const NativeVec3& recorded) {
				if (vertex < 0 || static_cast<size_t>(vertex) >= vertexCount)
					return;
				const NativeVec3& current = live[static_cast<size_t>(vertex)];
				const float dx = recorded.x - current.x;
				const float dy = recorded.y - current.y;
				const float dz = recorded.z - current.z;
				if (dx * dx + dy * dy + dz * dz > 1e-8f) {
					positionsMatch = false;
					return;
				}
				const auto ours = g_written.find(VertexKey(static_cast<int>(graphIndex), vertex));
				if (ours != g_written.end() &&
					std::memcmp(&ours->second, &recorded, sizeof(NativeVec3)) == 0)
					return; // continua onde nos deixamos: o pincel nao voltou nele
				moved.push_back({static_cast<int>(graphIndex), vertex});
			});
		if (!walked)
			return false;
	}
	if (!positionsMatch) {
		moved.clear();
		return false;
	}
	return true;
}

void ApplyCorrections(const std::vector<ContactCorrection>& corrections,
					  const std::vector<ContactMesh>& bound,
					  const std::vector<int>& strokeIndexOfGraphMesh, uintptr_t shapesBegin) {
	std::vector<bool> touched(g_frozen.size(), false);
	for (const ContactCorrection& correction : corrections) {
		const size_t graphIndex = static_cast<size_t>(correction.vertex.mesh);
		if (graphIndex >= bound.size() || !bound[graphIndex].positions)
			continue;
		const int strokeIndex = strokeIndexOfGraphMesh[graphIndex];
		if (strokeIndex < 0)
			continue;
		const size_t vertex = static_cast<size_t>(correction.vertex.vertex);
		if (vertex >= bound[graphIndex].vertexCount)
			continue;

		NativeVec3* live = const_cast<NativeVec3*>(
			reinterpret_cast<const NativeVec3*>(bound[graphIndex].positions));
		const NativeVec3 before = live[vertex];
		const NativeVec3 after = {correction.position.x, correction.position.y,
								  correction.position.z};
		const uintptr_t shape =
			shapesBegin + static_cast<size_t>(strokeIndex) * ActiveProfile()->undoShapeStride;

		// A partida so entra se ninguem gravou ainda -- na primeira vez que
		// mexemos neste vertice, o que esta la e a posicao do inicio do traco.
		if (!PutInMap(shape + kShapeStartMap, correction.vertex.vertex, before, true))
			continue;
		if (!PutInMap(shape + kShapeEndMap, correction.vertex.vertex, after, false))
			continue;

		live[vertex] = after;
		g_written[VertexKey(correction.vertex.mesh, correction.vertex.vertex)] = after;
		touched[graphIndex] = true;
	}

	// Equivale a Mesh::QueueUpdate(Position): o paint seguinte sobe o buffer
	// novo para o OpenGL. Sem isto a correcao existe no modelo e nao na tela.
	for (size_t graphIndex = 0; graphIndex < touched.size(); ++graphIndex)
		if (touched[graphIndex])
			reinterpret_cast<unsigned char*>(g_frozen[graphIndex].nativeMesh)
				[kMeshQueuePositionOffset] = 1;
}

// Tudo o que este mod faz depois do traco. Separado porque nada daqui pode
// escapar para dentro do host: e uma funcao C++ nossa chamada de um frame que
// nao sabe o que fazer com uma excecao nossa.
void CorrectAfterStroke(uintptr_t stroke, std::vector<uintptr_t>& strokeMeshes) {
	if (g_graph.clusters.empty() && g_graph.onEdges.empty())
		return;
	if (!ReadStrokeMeshes(stroke, strokeMeshes))
		return;
	uintptr_t shapesBegin = 0;
	if (!ReadUndoShapes(stroke, strokeMeshes.size(), shapesBegin))
		return;

	std::vector<int> strokeIndexOfGraphMesh;
	const std::vector<ContactMesh> bound = BindMeshes(strokeMeshes, strokeIndexOfGraphMesh);
	std::vector<ContactVertex> moved;
	if (!CollectBrushMoved(bound, strokeIndexOfGraphMesh, shapesBegin, moved) || moved.empty())
		return;

	const std::vector<ContactCorrection> corrections =
		SolveContacts(g_graph, bound, moved, g_falloffRings);
	if (!corrections.empty())
		ApplyCorrections(corrections, bound, strokeIndexOfGraphMesh, shapesBegin);
}

void __fastcall UpdateStrokeHook(void* strokeObject, void* pickInfo) {
	const uintptr_t stroke = reinterpret_cast<uintptr_t>(strokeObject);

	// Lido ANTES de chamar o original, que e quem zera o flag: e assim que se
	// sabe que um traco novo comecou e que o que gravamos antes nao vale mais.
	bool strokeStarting = false;
	if (stroke && RangeReadable(stroke + kStrokeFirstUpdate, 1))
		strokeStarting = *reinterpret_cast<const unsigned char*>(stroke + kStrokeFirstUpdate) != 0;

	// O grafo tem de ser congelado ANTES de o pincel encostar na malha. Montar
	// depois da primeira passada leria um seam que o pincel acabou de abrir, e
	// congelaria justamente a ausencia de contato que existimos para evitar.
	std::vector<uintptr_t> strokeMeshes;
	if (g_enabled && stroke && strokeStarting) {
		try {
			g_written.clear();
			if (!g_graphFrozen && ReadStrokeMeshes(stroke, strokeMeshes))
				FreezeGraph(strokeMeshes);
		}
		catch (...) {
			LogF("magnetismo: excecao ao congelar o grafo; magnetismo desligado");
			g_enabled = false;
		}
	}

	// O traco do usuario acontece SEMPRE, com magnetismo ou sem. O ponteiro foi
	// resolvido e conferido na instalacao para nao depender de nada aqui.
	if (g_originalUpdateStroke)
		g_originalUpdateStroke(strokeObject, pickInfo);

	if (!g_enabled || !stroke || !g_graphFrozen)
		return;
	try {
		CorrectAfterStroke(stroke, strokeMeshes);
	}
	catch (...) {
		LogF("magnetismo: excecao durante a correcao; magnetismo desligado");
		g_enabled = false;
	}
}

// Confere que o call site e mesmo `call updateStroke` e que updateStroke e
// mesmo a funcao que medimos. Duas perguntas diferentes, as duas obrigatorias.
bool RelativeBranchTargets(uintptr_t instruction, unsigned char opcode, uintptr_t target) {
	if (!RangeReadable(instruction, 5) ||
		*reinterpret_cast<const unsigned char*>(instruction) != opcode)
		return false;
	int32_t relative = 0;
	std::memcpy(&relative, reinterpret_cast<const void*>(instruction + 1), sizeof(relative));
	return static_cast<uintptr_t>(static_cast<int64_t>(instruction) + 5 + relative) == target;
}

bool InstallHook() {
	const uintptr_t base = HostBase();
	g_profile = RunningProfile();
	if (!base || !g_profile)
		return false;
	const uintptr_t updateThunk = base + g_profile->updateStrokeThunkRva;
	const uintptr_t updateBody = base + g_profile->updateStrokeBodyRva;
	const uintptr_t tryEmplaceThunk = base + g_profile->tryEmplaceThunkRva;
	const uintptr_t tryEmplaceBody = base + g_profile->tryEmplaceBodyRva;
	if (!RelativeBranchTargets(updateThunk, 0xE9, updateBody) ||
		!RangeReadable(updateBody, kUpdateStrokePrologue.size()) ||
		std::memcmp(reinterpret_cast<const void*>(updateBody), kUpdateStrokePrologue.data(),
					kUpdateStrokePrologue.size()) != 0) {
		LogF("magnetismo: assinatura de updateStroke nao confere");
		return false;
	}
	if (!RelativeBranchTargets(tryEmplaceThunk, 0xE9, tryEmplaceBody) ||
		!RangeReadable(tryEmplaceBody, kTryEmplacePrologue.size()) ||
		std::memcmp(reinterpret_cast<const void*>(tryEmplaceBody), kTryEmplacePrologue.data(),
					kTryEmplacePrologue.size()) != 0) {
		LogF("magnetismo: assinatura do helper de undo nao confere");
		return false;
	}
	for (uintptr_t rva : g_profile->updateStrokeCallRvas) {
		if (!RelativeBranchTargets(base + rva, 0xE8, updateThunk)) {
			LogF("magnetismo: call site 0x%llX nao chama updateStroke",
				 static_cast<unsigned long long>(rva));
			return false;
		}
	}

	// Resolvido antes de remendar o primeiro call site: dali em diante o hook
	// pode ser chamado, e ele precisa saber para onde mandar o traco de volta.
	g_originalUpdateStroke = reinterpret_cast<UpdateStroke>(updateThunk);

	std::array<uintptr_t, 4> sites = {};
	for (size_t i = 0; i < sites.size(); ++i)
		sites[i] = base + g_profile->updateStrokeCallRvas[i];
	const uintptr_t target = reinterpret_cast<uintptr_t>(&UpdateStrokeHook);
	const uintptr_t relay = CreateRelayNear(sites, target);
	if (!relay) {
		LogF("magnetismo: nao foi possivel alocar relay perto do codigo do host");
		g_originalUpdateStroke = nullptr;
		return false;
	}
	g_hookRelay = reinterpret_cast<void*>(relay);
	for (size_t i = 0; i < g_profile->updateStrokeCallRvas.size(); ++i) {
		const uintptr_t site = base + g_profile->updateStrokeCallRvas[i];
		const int64_t distance = static_cast<int64_t>(relay) - static_cast<int64_t>(site + 5);
		if (distance < (std::numeric_limits<int32_t>::min)() ||
			distance > (std::numeric_limits<int32_t>::max)()) {
			LogF("magnetismo: relay longe demais do call site para um call de 5 bytes");
			for (size_t undo = 0; undo < i; ++undo) {
				WriteCode(g_patches[undo].address, g_patches[undo].original.data(), 5);
				g_patches[undo] = CallPatch();
			}
			ReleaseRelay();
			g_originalUpdateStroke = nullptr;
			return false;
		}
		CallPatch& patch = g_patches[i];
		patch.address = site;
		std::memcpy(patch.original.data(), reinterpret_cast<const void*>(site), 5);
		unsigned char call[5] = {0xE8};
		const int32_t relative = static_cast<int32_t>(distance);
		std::memcpy(call + 1, &relative, sizeof(relative));
		if (!WriteCode(site, call, sizeof(call))) {
			for (size_t undo = 0; undo < i; ++undo) {
				WriteCode(g_patches[undo].address, g_patches[undo].original.data(), 5);
				g_patches[undo] = CallPatch();
			}
			ReleaseRelay();
			g_originalUpdateStroke = nullptr;
			return false;
		}
		patch.installed = true;
	}
	LogF("magnetismo: hook do traco instalado em %zu call site(s)",
		g_profile->updateStrokeCallRvas.size());
	return true;
}

void RemoveHook() {
	for (CallPatch& patch : g_patches) {
		if (patch.installed)
			WriteCode(patch.address, patch.original.data(), patch.original.size());
		patch = CallPatch();
	}
	ReleaseRelay();
	g_originalUpdateStroke = nullptr;
	g_profile = nullptr;
}

// --- item de menu -----------------------------------------------------------

constexpr UINT_PTR kFrameSubclassId = 0xB541;

HWND g_frame = nullptr;
HMENU g_shapeMenu = nullptr;
UINT g_toggleId = 0;
float g_menuTolerance = 0.0f;
int g_menuRings = 1;
bool g_menuInstalled = false;

std::wstring LocalMenuTextAt(HMENU menu, int position) {
	wchar_t text[256] = {};
	GetMenuStringW(menu, position, text, static_cast<int>(std::size(text)), MF_BYPOSITION);
	std::wstring out(text);
	const size_t tab = out.find(L'\t');
	if (tab != std::wstring::npos)
		out.resize(tab);
	while (!out.empty() && out.back() == L'.')
		out.pop_back();
	return out;
}

bool MenuUsesId(HMENU menu, UINT id) {
	if (!menu)
		return false;
	for (int i = 0; i < GetMenuItemCount(menu); ++i) {
		if (HMENU child = GetSubMenu(menu, i)) {
			if (MenuUsesId(child, id))
				return true;
		}
		else if (GetMenuItemID(menu, i) == id)
			return true;
	}
	return false;
}

UINT FreeId(HWND frame, UINT first) {
	for (UINT id = first; id < first + 32; ++id)
		if (!MenuUsesId(GetMenu(frame), id))
			return id;
	return 0;
}

HMENU FindShapeMenu(HWND frame) {
	HMENU bar = GetMenu(frame);
	if (!bar)
		return nullptr;
	for (int i = 0; i < GetMenuItemCount(bar); ++i)
		if (_wcsicmp(LocalMenuTextAt(bar, i).c_str(), L"Shape") == 0)
			return GetSubMenu(bar, i);
	return nullptr;
}

void SyncCheckmark() {
	if (g_shapeMenu && g_toggleId)
		CheckMenuItem(g_shapeMenu, g_toggleId,
					  MF_BYCOMMAND | (g_enabled ? MF_CHECKED : MF_UNCHECKED));
}

void Toggle() {
	if (g_enabled) {
		SeamMagnetism::Disable();
	}
	else if (!SeamMagnetism::Enable(g_menuTolerance, g_menuRings)) {
		// Chegar aqui quer dizer que o binario passou no SHA-256 e mesmo assim
		// a desmontagem nao bate -- outro mod ja remendou o mesmo lugar, por
		// exemplo. Ficar calado deixaria o item piscando sem explicacao.
		MessageBoxW(g_frame,
					L"O magnetismo de seams nao pode ser ligado: o codigo do traco do pincel "
					L"nao esta como o esperado nesta instalacao.",
					L"BSOS Improvements", MB_OK | MB_ICONWARNING);
	}
	SyncCheckmark();
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
						   DWORD_PTR) {
	if (msg == WM_COMMAND && HIWORD(wParam) == 0 && lParam == 0 && g_toggleId &&
		LOWORD(wParam) == g_toggleId) {
		Toggle();
		return 0;
	}
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, FrameProc, id);
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void InstallHere(void*) {
	g_shapeMenu = FindShapeMenu(g_frame);
	if (!g_shapeMenu)
		return;
	g_toggleId = FreeId(g_frame, 0xBF90);
	if (!g_toggleId)
		return;
	if (!AppendMenuW(g_shapeMenu, MF_SEPARATOR, 0, nullptr) ||
		!AppendMenuW(g_shapeMenu, MF_STRING | MF_UNCHECKED, g_toggleId, L"Seam Magnetism"))
		return;
	if (!SetWindowSubclass(g_frame, FrameProc, kFrameSubclassId, 0)) {
		RemoveMenu(g_shapeMenu, g_toggleId, MF_BYCOMMAND);
		return;
	}
	g_menuInstalled = true;
}

} // namespace

namespace SeamMagnetism {

bool Enable(float tolerance, int falloffRings) {
	if (g_enabled)
		return true;
	if (!HostBuild::IsSupportedOutfitStudio()) {
		LogF("magnetismo: build do host nao reconhecida; nada instalado");
		return false;
	}
	if (!(tolerance > 0.0f) || falloffRings < 1) {
		LogF("magnetismo: tolerancia ou aneis invalidos");
		return false;
	}
	g_tolerance = tolerance;
	g_falloffRings = falloffRings;
	g_graphFrozen = false;
	g_graph = ContactGraph();
	g_frozen.clear();
	g_written.clear();
	if (!InstallHook())
		return false;
	g_enabled = true;
	return true;
}

void Disable() {
	RemoveHook();
	g_enabled = false;
	g_graphFrozen = false;
	g_graph = ContactGraph();
	g_frozen.clear();
	g_written.clear();
}

bool IsEnabled() {
	return g_enabled;
}

bool Install(HWND frame, float tolerance, int falloffRings) {
	Uninstall();
	if (!frame || !HostBuild::IsSupportedOutfitStudio())
		return false;
	g_frame = frame;
	g_menuTolerance = tolerance;
	g_menuRings = falloffRings;
	// O item entra desmarcado e nao mexe em nada ate ser clicado. Instalar so
	// quando ja estivesse ligado faria a opcao so aparecer para quem ja soubesse
	// que ela existe.
	if (!RunOnUiThread(frame, InstallHere, nullptr) || !g_menuInstalled) {
		Uninstall();
		return false;
	}
	return true;
}

void Uninstall() {
	Disable();
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameProc, kFrameSubclassId);
	if (g_shapeMenu && g_toggleId) {
		RemoveMenu(g_shapeMenu, g_toggleId, MF_BYCOMMAND);
		const int last = GetMenuItemCount(g_shapeMenu) - 1;
		if (last >= 0 && LocalMenuTextAt(g_shapeMenu, last).empty())
			RemoveMenu(g_shapeMenu, last, MF_BYPOSITION); // o separador que entrou junto
	}
	g_frame = nullptr;
	g_shapeMenu = nullptr;
	g_toggleId = 0;
	g_menuInstalled = false;
}

} // namespace SeamMagnetism
