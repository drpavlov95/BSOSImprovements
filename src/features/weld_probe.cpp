#include "features/weld_probe.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/ui_thread.h"
#include "features/native_outfit_mesh.h"
#include "win32/winfind.h"

namespace {

// As guardas de memoria abaixo sao uma copia deliberada das de
// native_outfit_mesh.cpp. Instrumentacao descartavel nao deve poder quebrar o
// caminho de producao, e compartilhar codigo com ele criaria exatamente essa
// possibilidade. Quando este arquivo for apagado, nada fica para tras.

constexpr UINT_PTR kFrameSubclassId = 0xB541;
constexpr UINT_PTR kWatchTimerId = 0xB542;
constexpr UINT_PTR kPlantTimerId = 0xB543;

// Layout de Mesh na 5.8.2, medido em native_outfit_mesh.cpp.
constexpr size_t kMeshVertexCountOffset = 180;
constexpr size_t kMeshVerticesOffset = 184;
constexpr size_t kMeshUvsOffset = 232;
constexpr size_t kMeshMaskOffset = 240;
constexpr size_t kMeshTrianglesOffset = 256;

// Ate onde varrer procurando arrays por-vertice dentro do objeto.
constexpr size_t kScanLimit = 512;

// Quantos vertices amostrar ao avaliar um array candidato.
constexpr int kSampleCount = 192;

struct Vec3f { float x = 0.0f, y = 0.0f, z = 0.0f; };

HWND g_frame = nullptr;
HWND g_canvas = nullptr;
HMENU g_shapeMenu = nullptr;
HMENU g_probeMenu = nullptr;
UINT g_reportId = 0;
UINT g_plantId = 0;
UINT g_watchId = 0;
UINT g_displaceId = 0;
UINT g_rereadId = 0;
UINT g_restoreId = 0;
UINT g_measureId = 0;
UINT g_measureReportId = 0;
bool g_installed = false;

// Descoberto pelo experimento 0 e usado pelos experimentos 1 e 2.
size_t g_normsOffset = 0;

// Estado do experimento 2.
uintptr_t g_watchMesh = 0;
std::vector<int> g_watchIndices;
DWORD g_watchStart = 0;
int g_watchTicks = 0;

// Estado do experimento 3, para poder desfazer o que escrevemos.
uintptr_t g_displacedMesh = 0;
std::vector<Vec3f> g_vertsBefore;
std::vector<Vec3f> g_vertsWritten;

// Estado do experimento 7: replantio continuo e medida da direcao do
// deslocamento. Nada aqui e escrito em verts -- quem move a malha e o pincel.
uintptr_t g_measureMesh = 0;
std::vector<Vec3f> g_measureBefore;
std::vector<Vec3f> g_measureNormals;
std::vector<Vec3f> g_plantBuffer;
int g_plantTicks = 0;
int g_plantFailures = 0;

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
	std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
	return true;
}

float Length(const Vec3f& v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Normais geometricas calculadas a partir dos triangulos, ponderadas por area
// (o cross product nao normalizado JA carrega o dobro da area do triangulo).
//
// Isto e o gabarito do experimento 0: o array de normais do host concorda com
// este calculo, e tangentes e bitangentes -- que tambem sao unitarias e por
// isso passariam num teste de "parece uma normal" -- nao concordam.
std::vector<Vec3f> GeometricNormals(const SeamMesh& mesh) {
	std::vector<Vec3f> normals(mesh.vertices.size());
	for (const SeamTriangle& tri : mesh.triangles) {
		if (tri.a >= mesh.vertices.size() || tri.b >= mesh.vertices.size() ||
			tri.c >= mesh.vertices.size())
			continue;
		const SeamVertex& a = mesh.vertices[tri.a];
		const SeamVertex& b = mesh.vertices[tri.b];
		const SeamVertex& c = mesh.vertices[tri.c];
		const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
		const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
		const Vec3f face = {e1y * e2z - e1z * e2y,
							e1z * e2x - e1x * e2z,
							e1x * e2y - e1y * e2x};
		for (uint16_t index : {tri.a, tri.b, tri.c}) {
			normals[index].x += face.x;
			normals[index].y += face.y;
			normals[index].z += face.z;
		}
	}
	for (Vec3f& n : normals) {
		const float length = Length(n);
		if (length > 1e-12f) {
			n.x /= length;
			n.y /= length;
			n.z /= length;
		}
	}
	return normals;
}

bool MeshBasics(uintptr_t mesh, int& count, uintptr_t& verts) {
	return ReadValue(mesh + kMeshVertexCountOffset, count) &&
		   ReadValue(mesh + kMeshVerticesOffset, verts) &&
		   count > 0 && count <= 65535 &&
		   RangeHasProtection(verts, static_cast<size_t>(count) * sizeof(Vec3f), false);
}

const char* KnownOffsetName(size_t offset) {
	if (offset == kMeshVerticesOffset) return " <- verts (conhecido)";
	if (offset == kMeshUvsOffset) return " <- uvs (conhecido)";
	if (offset == kMeshMaskOffset) return " <- mask (conhecido)";
	if (offset == kMeshTrianglesOffset) return " <- triangles (conhecido)";
	return "";
}

void Report(const wchar_t* text) {
	MessageBoxW(g_frame, text, L"Weld Probe", MB_OK | MB_ICONINFORMATION);
}

bool TakeSnapshot(NativeOutfitMesh::Snapshot& snapshot) {
	const NativeOutfitMesh::Result result = NativeOutfitMesh::SnapshotSelected(snapshot);
	if (result == NativeOutfitMesh::Result::Ok)
		return true;
	if (result == NativeOutfitMesh::Result::NoSelectedMesh)
		Report(L"Selecione uma shape e tente de novo.");
	else if (result == NativeOutfitMesh::Result::UnsupportedBinary)
		Report(L"Esta build do Outfit Studio nao e a 5.8.2 verificada.");
	else
		Report(L"O layout da mesh nao pode ser lido com seguranca.");
	return false;
}

// ---------------------------------------------------------------------------
// Experimento 0: qual offset guarda o array de normais.
// Somente leitura.
// ---------------------------------------------------------------------------
void ExperimentReportArrays() {
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;

	const int count = static_cast<int>(snapshot.mesh.vertices.size());
	const std::vector<Vec3f> reference = GeometricNormals(snapshot.mesh);
	const int step = (std::max)(1, count / kSampleCount);

	LogF("weld probe: mesh=%p vertices=%d triangulos=%zu",
		 reinterpret_cast<void*>(snapshot.nativeMesh), count, snapshot.mesh.triangles.size());
	LogF("weld probe: procurando array de normais (amostra de %d vertices, passo %d)",
		 (std::min)(count, kSampleCount), step);

	size_t best = 0;
	float bestAgreement = 0.0f;
	for (size_t offset = 0; offset + sizeof(uintptr_t) <= kScanLimit; offset += sizeof(uintptr_t)) {
		uintptr_t array = 0;
		if (!ReadValue(snapshot.nativeMesh + offset, array) || !array)
			continue;
		if (!RangeHasProtection(array, static_cast<size_t>(count) * sizeof(Vec3f), false))
			continue;

		int sampled = 0, unit = 0;
		float dotSum = 0.0f, absDotSum = 0.0f;
		for (int i = 0; i < count; i += step) {
			Vec3f value = {};
			std::memcpy(&value, reinterpret_cast<const void*>(
							array + static_cast<size_t>(i) * sizeof(Vec3f)), sizeof(value));
			if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
				break;
			++sampled;
			const float length = Length(value);
			if (std::fabs(length - 1.0f) < 0.02f)
				++unit;
			const Vec3f& want = reference[static_cast<size_t>(i)];
			const float dot = value.x * want.x + value.y * want.y + value.z * want.z;
			dotSum += dot;
			absDotSum += std::fabs(dot);
		}
		if (sampled < 8)
			continue;

		const float unitRatio = static_cast<float>(unit) / static_cast<float>(sampled);
		const float meanDot = dotSum / static_cast<float>(sampled);
		const float meanAbsDot = absDotSum / static_cast<float>(sampled);

		// Somente candidatos que pelo menos PARECEM um campo de direcoes. Sem
		// este corte o log enche de arrays de posicao e de peso.
		if (unitRatio < 0.80f)
			continue;

		LogF("weld probe:   +%3zu unitario=%.0f%% dot=%+.3f |dot|=%.3f%s",
			 offset, unitRatio * 100.0f, meanDot, meanAbsDot, KnownOffsetName(offset));
		if (meanAbsDot > bestAgreement) {
			bestAgreement = meanAbsDot;
			best = offset;
		}
	}

	if (bestAgreement > 0.90f) {
		g_normsOffset = best;
		LogF("weld probe: normais em +%zu (concordancia %.3f)", best, bestAgreement);
		wchar_t text[256] = {};
		swprintf_s(text, L"Array de normais identificado em +%zu do objeto Mesh.\n"
						 L"Concordancia com a normal geometrica: %.3f\n\n"
						 L"Os experimentos 1 e 2 agora podem rodar.",
				   best, static_cast<double>(bestAgreement));
		Report(text);
		return;
	}

	LogF("weld probe: nenhum candidato concordou com a normal geometrica (melhor %.3f em +%zu)",
		 bestAgreement, best);
	Report(L"Nenhum array de normais foi identificado com confianca.\n"
		   L"O log tem os candidatos e a concordancia de cada um.");
}

// ---------------------------------------------------------------------------
// Experimento 1: o pincel le Mesh::norms na hora de agir?
//
// Planta (0,0,1) em TODAS as normais. Se o Inflate le o array, a malha inteira
// passa a inflar reto para cima em vez de para fora, e isso se ve sem log.
// ---------------------------------------------------------------------------
void ExperimentPlantNormals() {
	if (!g_normsOffset) {
		Report(L"Rode o experimento 0 primeiro: o offset das normais e descoberto la.");
		return;
	}
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;

	const int count = static_cast<int>(snapshot.mesh.vertices.size());
	uintptr_t normals = 0;
	if (!ReadValue(snapshot.nativeMesh + g_normsOffset, normals) ||
		!RangeHasProtection(normals, static_cast<size_t>(count) * sizeof(Vec3f), true)) {
		Report(L"O array de normais nao esta gravavel agora. Rode o experimento 0 de novo.");
		return;
	}

	Vec3f* target = reinterpret_cast<Vec3f*>(normals);
	for (int i = 0; i < count; ++i)
		target[i] = Vec3f{0.0f, 0.0f, 1.0f};

	// De proposito nao pedimos atualizacao de normais ao host: pedir faria ele
	// recalcular e apagar exatamente o que acabamos de plantar.
	g_watchMesh = snapshot.nativeMesh;
	LogF("weld probe: (0,0,1) plantado em %d normais de %p",
		 count, reinterpret_cast<void*>(snapshot.nativeMesh));
	Report(L"Normais plantadas apontando para +Z em toda a shape.\n\n"
		   L"Agora de UMA pincelada de Inflate.\n\n"
		   L"Se a malha subir reto (como um cisalhamento), o pincel le o array.\n"
		   L"Se ela inflar normalmente para fora, ele nao le -- ou recalculou antes.\n\n"
		   L"O sombreamento nao muda: a GPU ainda tem as normais antigas.");
}

// ---------------------------------------------------------------------------
// Experimento 2: quando o host recalcula as normais?
//
// Amostra as normais plantadas a cada 100 ms e registra o instante em que
// deixam de ser (0,0,1). Isso mede o problema de timing em vez de supor.
// ---------------------------------------------------------------------------
void ExperimentWatchNormals() {
	if (!g_normsOffset) {
		Report(L"Rode o experimento 0 primeiro.");
		return;
	}
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;

	const int count = static_cast<int>(snapshot.mesh.vertices.size());
	g_watchMesh = snapshot.nativeMesh;
	g_watchIndices.clear();
	for (int i = 0; i < 8; ++i)
		g_watchIndices.push_back(count * i / 8);
	g_watchStart = GetTickCount();
	g_watchTicks = 0;
	SetTimer(g_frame, kWatchTimerId, 100, nullptr);
	LogF("weld probe: vigiando 8 normais por 20 s");
	Report(L"Vigiando as normais plantadas por 20 segundos.\n\n"
		   L"Comece a arrastar o Inflate AGORA e mantenha o arrasto.\n\n"
		   L"O log registra o instante em que o host sobrescreve o que plantamos.");
}

void WatchTick() {
	++g_watchTicks;
	const DWORD elapsed = GetTickCount() - g_watchStart;
	uintptr_t normals = 0;
	if (!g_watchMesh || !ReadValue(g_watchMesh + g_normsOffset, normals) || !normals) {
		KillTimer(g_frame, kWatchTimerId);
		LogF("weld probe: vigia parou -- a mesh saiu de baixo dela em %lu ms", elapsed);
		return;
	}

	int intact = 0;
	for (int index : g_watchIndices) {
		Vec3f value = {};
		if (!ReadValue(normals + static_cast<size_t>(index) * sizeof(Vec3f), value))
			continue;
		if (std::fabs(value.x) < 0.01f && std::fabs(value.y) < 0.01f &&
			std::fabs(value.z - 1.0f) < 0.01f)
			++intact;
	}

	if (intact < static_cast<int>(g_watchIndices.size())) {
		KillTimer(g_frame, kWatchTimerId);
		LogF("weld probe: normais sobrescritas em %lu ms (%d de %zu ainda intactas)",
			 elapsed, intact, g_watchIndices.size());
		return;
	}
	if (elapsed >= 20000) {
		KillTimer(g_frame, kWatchTimerId);
		LogF("weld probe: 20 s e as 8 normais plantadas sobreviveram -- o host nao recalculou");
		return;
	}
	if (g_watchTicks % 10 == 0)
		LogF("weld probe: %lu ms, as 8 normais plantadas seguem intactas", elapsed);
}

// ---------------------------------------------------------------------------
// Experimento 3: o que escrevemos em Mesh::verts chega no modelo de dados?
//
// Desloca a shape 2 unidades em X. O deslocamento e guardado para poder ser
// desfeito, porque nada disso passa pelo undo do host.
// ---------------------------------------------------------------------------
void ExperimentDisplaceVerts() {
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;

	int count = 0;
	uintptr_t verts = 0;
	if (!MeshBasics(snapshot.nativeMesh, count, verts) ||
		!RangeHasProtection(verts, static_cast<size_t>(count) * sizeof(Vec3f), true)) {
		Report(L"O array de vertices nao esta gravavel agora.");
		return;
	}

	Vec3f* target = reinterpret_cast<Vec3f*>(verts);
	g_displacedMesh = snapshot.nativeMesh;
	g_vertsBefore.assign(target, target + count);
	for (int i = 0; i < count; ++i)
		target[i].x += 2.0f;
	g_vertsWritten.assign(target, target + count);

	LogF("weld probe: %d vertices deslocados +2 em X em %p",
		 count, reinterpret_cast<void*>(snapshot.nativeMesh));
	Report(L"Shape deslocada 2 unidades em X na memoria.\n\n"
		   L"A tela nao muda ainda: a GPU tem as posicoes antigas.\n\n"
		   L"Agora force o host a recalcular -- mexa em qualquer slider, ou\n"
		   L"selecione outra shape e volte -- e rode o experimento 5.\n\n"
		   L"NAO SALVE o projeto, a nao ser que ele seja descartavel.");
}

void ExperimentRereadVerts() {
	if (g_vertsWritten.empty()) {
		Report(L"Rode o experimento 4 primeiro.");
		return;
	}
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;
	if (snapshot.nativeMesh != g_displacedMesh) {
		Report(L"A shape selecionada nao e a que foi deslocada.");
		return;
	}

	int count = 0;
	uintptr_t verts = 0;
	if (!MeshBasics(snapshot.nativeMesh, count, verts) ||
		count != static_cast<int>(g_vertsWritten.size())) {
		Report(L"A mesh mudou de tamanho -- o host a reconstruiu. Recomece.");
		return;
	}

	const Vec3f* now = reinterpret_cast<const Vec3f*>(verts);
	int matchesWritten = 0, matchesBefore = 0, neither = 0;
	for (int i = 0; i < count; ++i) {
		const bool written = std::fabs(now[i].x - g_vertsWritten[static_cast<size_t>(i)].x) < 0.001f;
		const bool before = std::fabs(now[i].x - g_vertsBefore[static_cast<size_t>(i)].x) < 0.001f;
		if (written) ++matchesWritten;
		else if (before) ++matchesBefore;
		else ++neither;
	}

	LogF("weld probe: releitura -- %d como escrevemos, %d como estavam antes, %d nenhum dos dois",
		 matchesWritten, matchesBefore, neither);
	wchar_t text[512] = {};
	swprintf_s(text, L"De %d vertices:\n\n"
					 L"  %d ainda estao onde ESCREVEMOS\n"
					 L"  %d voltaram ao valor ANTERIOR\n"
					 L"  %d estao em outro lugar\n\n"
					 L"Muitos na primeira linha: a escrita vive no modelo de dados.\n"
					 L"Muitos na segunda: era copia de exibicao, e o host a reconstruiu.",
			   count, matchesWritten, matchesBefore, neither);
	Report(text);
}

void ExperimentRestoreVerts() {
	if (g_vertsBefore.empty()) {
		Report(L"Nada para restaurar.");
		return;
	}
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot) || snapshot.nativeMesh != g_displacedMesh) {
		Report(L"Selecione a shape que foi deslocada.");
		return;
	}
	int count = 0;
	uintptr_t verts = 0;
	if (!MeshBasics(snapshot.nativeMesh, count, verts) ||
		count != static_cast<int>(g_vertsBefore.size()) ||
		!RangeHasProtection(verts, static_cast<size_t>(count) * sizeof(Vec3f), true)) {
		Report(L"Nao e seguro restaurar agora: a mesh mudou.");
		return;
	}
	std::memcpy(reinterpret_cast<void*>(verts), g_vertsBefore.data(),
				g_vertsBefore.size() * sizeof(Vec3f));
	g_vertsBefore.clear();
	g_vertsWritten.clear();
	g_displacedMesh = 0;
	LogF("weld probe: %d vertices restaurados", count);
	Report(L"Posicoes originais restauradas na memoria.");
}

// ---------------------------------------------------------------------------
// Experimento 7: medir, sem depender de observacao, se a escultura sai na
// direcao que a gente plantou.
//
// O experimento 2 mostrou que as normais plantadas nao sobrevivem 109 ms. Em
// vez de brigar com isso, aqui elas sao REPLANTADAS a cada tick do timer, o
// que mantem o campo vivo durante o arrasto. Depois a direcao do deslocamento
// de cada vertice e comparada com duas hipoteses:
//
//   +Z              -> o pincel leu o array que plantamos
//   normal propria  -> o pincel usou a normal recalculada pelo host
//
// Vertices cuja normal geometrica JA aponta para perto de +Z sao descartados:
// neles as duas hipoteses dao a mesma resposta e o resultado nao discrimina.
// ---------------------------------------------------------------------------
void PlantTick() {
	++g_plantTicks;
	uintptr_t normals = 0;
	if (!g_measureMesh || !ReadValue(g_measureMesh + g_normsOffset, normals) || !normals ||
		!RangeHasProtection(normals, g_plantBuffer.size() * sizeof(Vec3f), true)) {
		++g_plantFailures;
		return;
	}
	std::memcpy(reinterpret_cast<void*>(normals), g_plantBuffer.data(),
				g_plantBuffer.size() * sizeof(Vec3f));
}

void ExperimentMeasureStart() {
	if (!g_normsOffset) {
		Report(L"Rode o experimento 0 primeiro.");
		return;
	}
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;

	int count = 0;
	uintptr_t verts = 0;
	if (!MeshBasics(snapshot.nativeMesh, count, verts)) {
		Report(L"O array de vertices nao pode ser lido agora.");
		return;
	}

	const Vec3f* current = reinterpret_cast<const Vec3f*>(verts);
	g_measureMesh = snapshot.nativeMesh;
	g_measureBefore.assign(current, current + count);
	g_measureNormals = GeometricNormals(snapshot.mesh);
	g_plantBuffer.assign(static_cast<size_t>(count), Vec3f{0.0f, 0.0f, 1.0f});
	g_plantTicks = 0;
	g_plantFailures = 0;

	// USER_TIMER_MINIMUM e 10 ms, o mais rapido que o Windows entrega. O host
	// desenha a ~16 ms, entao isto da mais de um replantio por frame.
	SetTimer(g_frame, kPlantTimerId, USER_TIMER_MINIMUM, nullptr);
	LogF("weld probe: replantio continuo ligado em %p (%d vertices)",
		 reinterpret_cast<void*>(snapshot.nativeMesh), count);
	Report(L"Replantio continuo ligado: as normais sao reescritas para +Z a cada 10 ms.\n\n"
		   L"Agora arraste o Inflate por uns 2 segundos, numa parte da malha que\n"
		   L"aponte PARA O LADO -- a lateral do tronco serve. Evite o topo da\n"
		   L"cabeca e os ombros: ali a normal ja aponta para cima e a medida\n"
		   L"nao distingue as duas hipoteses.\n\n"
		   L"Depois rode o item 8. Nao precisa observar nada.");
}

void ExperimentMeasureReport() {
	KillTimer(g_frame, kPlantTimerId);
	if (g_measureBefore.empty()) {
		Report(L"Rode o item 7 primeiro.");
		return;
	}
	NativeOutfitMesh::Snapshot snapshot;
	if (!TakeSnapshot(snapshot))
		return;
	if (snapshot.nativeMesh != g_measureMesh) {
		Report(L"A shape selecionada nao e a que estava sendo medida.");
		return;
	}

	int count = 0;
	uintptr_t verts = 0;
	if (!MeshBasics(snapshot.nativeMesh, count, verts) ||
		count != static_cast<int>(g_measureBefore.size())) {
		Report(L"A mesh mudou de tamanho durante a medida. Recomece.");
		return;
	}

	const Vec3f* now = reinterpret_cast<const Vec3f*>(verts);
	float largest = 0.0f;
	for (int i = 0; i < count; ++i) {
		const Vec3f& before = g_measureBefore[static_cast<size_t>(i)];
		const Vec3f delta = {now[i].x - before.x, now[i].y - before.y, now[i].z - before.z};
		largest = (std::max)(largest, Length(delta));
	}
	if (largest < 1e-6f) {
		LogF("weld probe: nenhum vertice se moveu (%d ticks de replantio, %d falhas)",
			 g_plantTicks, g_plantFailures);
		Report(L"Nenhum vertice se moveu -- a pincelada nao pegou esta shape.\n\n"
			   L"Confira que o Inflate estava ativo e que a shape medida e a que\n"
			   L"voce esculpiu, e repita o item 7.");
		return;
	}

	// Limiar relativo: escala de malha de .nif nao e conhecida de antemao.
	const float floor = largest * 0.05f;
	int moved = 0, discriminating = 0, alongZ = 0, alongOwn = 0, neither = 0;
	float zDotSum = 0.0f, ownDotSum = 0.0f;
	for (int i = 0; i < count; ++i) {
		const Vec3f& before = g_measureBefore[static_cast<size_t>(i)];
		Vec3f delta = {now[i].x - before.x, now[i].y - before.y, now[i].z - before.z};
		const float length = Length(delta);
		if (length < floor)
			continue;
		++moved;

		const Vec3f& own = g_measureNormals[static_cast<size_t>(i)];
		if (std::fabs(own.z) > 0.5f)
			continue; // a propria normal ja aponta para perto de +Z: nao decide
		++discriminating;

		delta.x /= length;
		delta.y /= length;
		delta.z /= length;
		const float zDot = delta.z;
		const float ownDot = delta.x * own.x + delta.y * own.y + delta.z * own.z;
		zDotSum += zDot;
		ownDotSum += ownDot;
		if (zDot > 0.85f) ++alongZ;
		else if (ownDot > 0.85f) ++alongOwn;
		else ++neither;
	}

	LogF("weld probe: replantio %d ticks, %d falhas | moveram=%d discriminantes=%d",
		 g_plantTicks, g_plantFailures, moved, discriminating);
	if (discriminating == 0) {
		LogF("weld probe: nenhum vertice discriminante -- regiao esculpida aponta para +Z");
		Report(L"Vertices se moveram, mas todos numa regiao cuja normal ja aponta\n"
			   L"para cima -- ali as duas hipoteses dao a mesma resposta.\n\n"
			   L"Repita o item 7 esculpindo a LATERAL do tronco.");
		return;
	}

	const float meanZ = zDotSum / static_cast<float>(discriminating);
	const float meanOwn = ownDotSum / static_cast<float>(discriminating);
	LogF("weld probe: +Z=%d propria=%d outro=%d | dot medio: +Z=%+.3f propria=%+.3f",
		 alongZ, alongOwn, neither, meanZ, meanOwn);

	wchar_t text[768] = {};
	swprintf_s(text,
		L"%d vertices se moveram, %d deles discriminantes.\n\n"
		L"Direcao do deslocamento:\n"
		L"  %d seguiram +Z (a normal que plantamos)\n"
		L"  %d seguiram a propria normal (a do host)\n"
		L"  %d nem uma nem outra\n\n"
		L"Dot medio:  +Z = %+.3f   propria = %+.3f\n\n"
		L"Replantio: %d ticks, %d falhas.\n\n"
		L"Maioria em +Z: o pincel le o array e a corrida e ganhavel.\n"
		L"Maioria na propria: nao da para influenciar o pincel por aqui.",
		moved, discriminating, alongZ, alongOwn, neither,
		static_cast<double>(meanZ), static_cast<double>(meanOwn),
		g_plantTicks, g_plantFailures);
	Report(text);
}

// --- plumbing de menu (espelha seam_masks.cpp) -----------------------------

std::wstring MenuTextAt(HMENU menu, int position) {
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
		if (_wcsicmp(MenuTextAt(bar, i).c_str(), L"Shape") == 0)
			return GetSubMenu(bar, i);
	return nullptr;
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
						   UINT_PTR id, DWORD_PTR) {
	if (msg == WM_TIMER && wParam == kWatchTimerId) {
		WatchTick();
		return 0;
	}
	if (msg == WM_TIMER && wParam == kPlantTimerId) {
		PlantTick();
		return 0;
	}
	if (msg == WM_COMMAND && HIWORD(wParam) == 0 && lParam == 0) {
		const UINT command = LOWORD(wParam);
		if (command == g_reportId) { ExperimentReportArrays(); return 0; }
		if (command == g_plantId) { ExperimentPlantNormals(); return 0; }
		if (command == g_watchId) { ExperimentWatchNormals(); return 0; }
		if (command == g_displaceId) { ExperimentDisplaceVerts(); return 0; }
		if (command == g_rereadId) { ExperimentRereadVerts(); return 0; }
		if (command == g_restoreId) { ExperimentRestoreVerts(); return 0; }
		if (command == g_measureId) { ExperimentMeasureStart(); return 0; }
		if (command == g_measureReportId) { ExperimentMeasureReport(); return 0; }
	}
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, FrameProc, id);
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void InstallHere(void*) {
	g_shapeMenu = FindShapeMenu(g_frame);
	if (!g_shapeMenu)
		return;

	UINT next = 0xBF90;
	UINT* const ids[] = {&g_reportId, &g_plantId, &g_watchId,
						 &g_displaceId, &g_rereadId, &g_restoreId,
						 &g_measureId, &g_measureReportId};
	for (UINT* slot : ids) {
		*slot = FreeId(g_frame, next);
		if (!*slot)
			return;
		next = *slot + 1;
	}

	g_probeMenu = CreatePopupMenu();
	if (!g_probeMenu)
		return;
	if (!AppendMenuW(g_probeMenu, MF_STRING, g_reportId, L"0 - Achar o array de normais") ||
		!AppendMenuW(g_probeMenu, MF_SEPARATOR, 0, nullptr) ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_plantId, L"1 - Plantar normais em +Z") ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_watchId, L"2 - Vigiar as normais plantadas") ||
		!AppendMenuW(g_probeMenu, MF_SEPARATOR, 0, nullptr) ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_displaceId, L"4 - Deslocar vertices +2 em X") ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_rereadId, L"5 - Reler os vertices") ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_restoreId, L"6 - Restaurar os vertices") ||
		!AppendMenuW(g_probeMenu, MF_SEPARATOR, 0, nullptr) ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_measureId,
					 L"7 - Replantar sem parar, e esculpir") ||
		!AppendMenuW(g_probeMenu, MF_STRING, g_measureReportId,
					 L"8 - Medir para onde a escultura foi") ||
		!AppendMenuW(g_shapeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(g_probeMenu),
					 L"Weld Probe (diagnostico)")) {
		DestroyMenu(g_probeMenu);
		g_probeMenu = nullptr;
		return;
	}
	if (!SetWindowSubclass(g_frame, FrameProc, kFrameSubclassId, 0)) {
		for (int i = GetMenuItemCount(g_shapeMenu) - 1; i >= 0; --i)
			if (GetSubMenu(g_shapeMenu, i) == g_probeMenu) {
				RemoveMenu(g_shapeMenu, i, MF_BYPOSITION);
				break;
			}
		DestroyMenu(g_probeMenu);
		g_probeMenu = nullptr;
		return;
	}
	DrawMenuBar(g_frame);
	g_installed = true;
}

} // namespace

namespace WeldProbe {

bool Install(HWND frame) {
	Uninstall();
	if (!frame || !NativeOutfitMesh::SupportsRunningBinary())
		return false;
	g_frame = frame;
	g_canvas = FindLargestVisibleByClass(frame, L"wxGLCanvas");
	if (!RunOnUiThread(frame, InstallHere, nullptr) || !g_installed) {
		Uninstall();
		return false;
	}
	LogF("weld probe: instrumentacao instalada em Shape > Weld Probe");
	return true;
}

void Uninstall() {
	if (g_frame && IsWindow(g_frame)) {
		KillTimer(g_frame, kWatchTimerId);
		KillTimer(g_frame, kPlantTimerId);
		RemoveWindowSubclass(g_frame, FrameProc, kFrameSubclassId);
	}
	if (g_shapeMenu && g_probeMenu) {
		for (int i = GetMenuItemCount(g_shapeMenu) - 1; i >= 0; --i)
			if (GetSubMenu(g_shapeMenu, i) == g_probeMenu) {
				RemoveMenu(g_shapeMenu, i, MF_BYPOSITION);
				break;
			}
		DestroyMenu(g_probeMenu);
	}
	g_frame = nullptr;
	g_canvas = nullptr;
	g_shapeMenu = nullptr;
	g_probeMenu = nullptr;
	g_reportId = g_plantId = g_watchId = g_displaceId = g_rereadId = g_restoreId = 0;
	g_measureId = g_measureReportId = 0;
	g_measureMesh = 0;
	g_measureBefore.clear();
	g_measureNormals.clear();
	g_plantBuffer.clear();
	g_normsOffset = 0;
	g_watchMesh = 0;
	g_watchIndices.clear();
	g_displacedMesh = 0;
	g_vertsBefore.clear();
	g_vertsWritten.clear();
	g_installed = false;
}

} // namespace WeldProbe
