#include "features/slider_reorder.h"

#include <commctrl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#include "core/host.h"
#include "core/log.h"
#include "core/osp.h"
#include "core/theme.h"
#include "features/pose_panel.h"
#include "features/zero_sliders.h" // PickSliderHost
#include "win32/menu_toggle.h"
#include "win32/menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

// A alca ocupa a folga que ja existia entre a caixa de marcacao e o nome, e
// empurra do nome para a direita.
//
// A medida saiu do proprio programa, registrada no dump: lapis em 0..22, caixa
// em 27..42, nome em 47..149, barra em 154..715, valor em 722..762. Nao ha um
// pixel livre ali, entao alguem tem que andar -- e o que anda e o nome e a
// barra, nunca o lapis nem a caixa, que sao os dois botoes que o usuario ja
// sabia onde encontrar.
const int kGripWidth = 14;
const int kGripId = 0xBF03;
const UINT_PTR kGripSubclassId = 0xB515;
const UINT_PTR kHostSubclassId = 0xB516;
const UINT_PTR kSlotSubclassId = 0xB517;
const UINT_PTR kFrameSubclassId = 0xB518;

// Pedido de "olhe a lista de novo", mandado pelo painel para ele mesmo.
const UINT kRecheckMsg = WM_APP + 0x1F;

HWND g_frame = nullptr;
HWND g_posePanel = nullptr;
bool g_installed = false;
UINT g_saveId = 0;
UINT g_saveAsId = 0;
UINT g_exitId = 0;
bool g_orderDirty = false;
bool g_titleMarked = false;
std::vector<std::wstring> g_desiredOrder;

void ClearOrderTitleMark();

struct FileStamp {
	uintmax_t size = 0;
	std::filesystem::file_time_type written = {};
};

using OspSnapshot = std::map<std::filesystem::path, FileStamp>;

std::filesystem::path SliderSetsFolder() {
	// ProjectUtil prefere o AppDir sempre que ele contem SliderSets. Essa e a
	// instalacao normal e tambem o caminho que passa pelo VFS do MO2.
	return std::filesystem::path(AppDir()) / L"SliderSets";
}

OspSnapshot SnapshotOspFiles() {
	OspSnapshot out;
	std::error_code ec;
	const auto root = SliderSetsFolder();
	if (!std::filesystem::is_directory(root, ec))
		return out;
	for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
		if (!it->is_regular_file(ec) || _wcsicmp(it->path().extension().c_str(), L".osp") != 0)
			continue;
		FileStamp stamp;
		stamp.size = it->file_size(ec);
		stamp.written = it->last_write_time(ec);
		if (!ec)
			out.emplace(it->path(), stamp);
	}
	return out;
}

std::vector<std::filesystem::path> ChangedOspFiles(const OspSnapshot& before,
												 const OspSnapshot& after) {
	std::vector<std::filesystem::path> changed;
	for (const auto& [path, stamp] : after) {
		auto old = before.find(path);
		if (old == before.end() || old->second.size != stamp.size || old->second.written != stamp.written)
			changed.push_back(path);
	}
	return changed;
}

std::string ReadBytes(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return std::string();
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool ReplaceBytesAtomically(const std::filesystem::path& path, const std::string& bytes) {
	wchar_t temp[MAX_PATH] = {};
	const std::wstring folder = path.parent_path().wstring();
	if (folder.size() >= MAX_PATH || !GetTempFileNameW(folder.c_str(), L"bso", 0, temp))
		return false;

	HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
						  FILE_ATTRIBUTE_TEMPORARY, nullptr);
	bool ok = file != INVALID_HANDLE_VALUE;
	size_t offset = 0;
	while (ok && offset < bytes.size()) {
		DWORD written = 0;
		const DWORD part = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 0x40000000u));
		ok = WriteFile(file, bytes.data() + offset, part, &written, nullptr) && written == part;
		offset += written;
	}
	if (ok)
		ok = FlushFileBuffers(file) != FALSE;
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	if (ok)
		ok = ReplaceFileW(path.c_str(), temp, nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
	if (!ok)
		DeleteFileW(temp);
	return ok;
}

std::vector<std::string> NarrowOrder() {
	std::vector<std::string> out;
	out.reserve(g_desiredOrder.size());
	for (const std::wstring& wide : g_desiredOrder) {
		if (wide.empty())
			return {};
		const int count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
										 nullptr, 0, nullptr, nullptr);
		if (count <= 0)
			return {};
		std::string name(static_cast<size_t>(count), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
						name.data(), count, nullptr, nullptr);
		out.push_back(std::move(name));
	}
	return out;
}

bool PersistOrderAfterSave(const OspSnapshot& before) {
	if (g_desiredOrder.empty())
		return false;
	const auto changed = ChangedOspFiles(before, SnapshotOspFiles());
	if (changed.size() != 1) {
		LogF("reorder: save terminou com %d arquivos .osp alterados; persistencia recusada",
			 static_cast<int>(changed.size()));
		return false;
	}

	const std::string original = ReadBytes(changed.front());
	const std::vector<std::string> order = NarrowOrder();
	std::string matched;
	const std::string reordered = ReorderUniqueMatchingOspSliderSet(original, order, &matched);
	if (reordered.empty()) {
		LogF("reorder: %ls nao possui um unico SliderSet com os %d sliders da tela",
			 changed.front().c_str(), static_cast<int>(order.size()));
		return false;
	}
	if (reordered != original && !ReplaceBytesAtomically(changed.front(), reordered)) {
		LogF("reorder: falha ao substituir %ls atomicamente (erro %lu)",
			 changed.front().c_str(), GetLastError());
		return false;
	}

	g_orderDirty = false;
	ClearOrderTitleMark();
	LogF("reorder: ordem salva em %ls, SliderSet '%s'", changed.front().c_str(), matched.c_str());
	return true;
}

void EnableSaveForOrder() {
	if (!g_frame || !g_saveId)
		return;
	if (HMENU menu = GetMenu(g_frame)) {
		EnableMenuItem(menu, g_saveId, MF_BYCOMMAND | MF_ENABLED);
		DrawMenuBar(g_frame);
	}
}

void MarkTitleForOrder() {
	if (!g_frame || g_titleMarked)
		return;
	std::wstring title;
	const int count = GetWindowTextLengthW(g_frame);
	title.resize(static_cast<size_t>(count > 0 ? count : 0) + 1, L'\0');
	const int written = GetWindowTextW(g_frame, title.data(), static_cast<int>(title.size()));
	title.resize(written > 0 ? static_cast<size_t>(written) : 0);
	const size_t suffix = title.rfind(L" - Outfit Studio");
	if (suffix == std::wstring::npos)
		return;
	// Se o Outfit Studio ja marcou o projeto, o prompt nativo cuidara dele.
	if (suffix > 0 && title[suffix - 1] == L'*')
		return;
	title.insert(suffix, 1, L'*');
	SetWindowTextW(g_frame, title.c_str());
	g_titleMarked = true;
}

void ClearOrderTitleMark() {
	if (!g_frame || !g_titleMarked)
		return;
	const int count = GetWindowTextLengthW(g_frame);
	std::wstring title(static_cast<size_t>(count > 0 ? count : 0) + 1, L'\0');
	const int written = GetWindowTextW(g_frame, title.data(), static_cast<int>(title.size()));
	title.resize(written > 0 ? static_cast<size_t>(written) : 0);
	const size_t suffix = title.rfind(L"* - Outfit Studio");
	if (suffix != std::wstring::npos)
		title.erase(suffix, 1);
	SetWindowTextW(g_frame, title.c_str());
	g_titleMarked = false;
}

// Uma linha da lista, com o nome que a identifica.
struct Row {
	HWND window = nullptr;
	std::wstring name;
	int top = 0;
};

// A ordem que o usuario escolheu, por NOME.
//
// Nao por HWND e nao por coordenada: o wx refaz o layout quando quer, e as
// janelas de ontem nao existem mais depois de trocar de outfit. O nome e a unica
// coisa do slider que sobrevive a isso.
HWND g_knownHost = nullptr;
DWORD g_lastCheck = 0;
bool g_recheckPending = false;

bool g_gripsOn = true;
bool g_dark = false;

// O arrasto em curso.
struct Drag {
	bool active = false;
	HWND host = nullptr;
	HWND slot = nullptr;        // o vao desenhado no lugar de destino
	std::vector<HWND> order;    // as linhas na ordem de agora
	std::vector<HWND> original; // como estavam quando o arrasto comecou
	std::vector<int> slotTops;
	int rowHeight = 0;
	int index = -1;
	int grabOffset = 0;
};

Drag g_drag;

RECT RectIn(HWND window, HWND parent) {
	RECT rc = {};
	GetWindowRect(window, &rc);
	MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
	return rc;
}

HWND GripOf(HWND row) {
	return GetDlgItem(row, kGripId);
}

std::wstring TextOf(HWND window) {
	const int len = GetWindowTextLengthW(window);
	if (len <= 0 || len > 512)
		return std::wstring();
	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	const int written = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

bool HasTrackbar(HWND window) {
	return !FindDescendantsByClass(window, TRACKBAR_CLASSW).empty();
}

// TODAS as linhas do painel, escondidas ou nao.
//
// Serve para reconstruir a ordem completa por nome, que precisa incluir o que o
// filtro escondeu.
std::vector<HWND> RowWindows(HWND host) {
	std::vector<HWND> out;
	for (HWND child : ChildrenOf(host)) {
		if (HasTrackbar(child))
			out.push_back(child);
	}
	return out;
}

// So as linhas que estao de fato na tela agora.
//
// Duas coisas se escondem atras desta pergunta, e as duas quebravam a feature.
//
// A primeira e o filtro de sliders do proprio Outfit Studio -- a caixa "Slider
// Filter" no alto do painel. Ele esconde linha por linha, e este mod nao sabia:
// mexia em todas e reposicionava a lista usando as posicoes das escondidas.
//
// A segunda e maior e so apareceu no dump: o Outfit Studio mantem um VIVEIRO de
// linhas prontas e escondidas, dezenas delas, todas empilhadas na mesma
// coordenada e com um Static chamado "sliderPoolDummy" dentro. Elas sao filhas
// diretas do mesmo sliderScroll e tem barra dentro, entao entravam na conta como
// se fossem linhas de verdade -- dezenas de lugares falsos no mesmo pixel, que a
// reordenacao entao distribuia. Era isso que fazia a lista aparecer e sumir sem
// regra.
//
// A pergunta e feita a PROPRIA janela, com HasVisibleStyle, e nao com
// IsWindowVisible -- pela mesma razao de sempre neste programa.
std::vector<HWND> VisibleRowWindows(HWND host) {
	std::vector<HWND> out;
	for (HWND window : RowWindows(host)) {
		if (HasVisibleStyle(window))
			out.push_back(window);
	}
	return out;
}

// Onde estao as pecas de uma linha, do jeito que ela esta AGORA.
//
// Tudo aqui e medido so entre os controles VISIVEIS. Cada linha carrega mais
// controles do que mostra -- o dump listou dois botoes de 22x22, um "-", um "+",
// a caixa, o nome, a barra e a porcentagem -- e o modo de edicao troca quais
// deles aparecem. Olhar para os escondidos ancorava a alca num botao que ninguem
// ve, e entrar no modo de edicao, que muda o elenco, mandava a alca para tras
// dele. Era por isso que a alca sumia ao clicar no lapis.
struct RowParts {
	HWND trackbar = nullptr;
	HWND name = nullptr;
	RECT trackbarRect = {};
	RECT nameRect = {};

	// Onde termina o controle que vem ANTES do nome -- a caixa de marcacao, no
	// modo normal. E dali que a alca comeca.
	int anchorRight = 0;

	// O nome e o que mais estiver entre ele e a barra. Andam juntos.
	std::vector<HWND> movable;

	bool ok = false;
};

RowParts FindParts(HWND row) {
	RowParts parts;
	HWND grip = GripOf(row);

	std::vector<HWND> visible;
	for (HWND child : ChildrenOf(row)) {
		if (child != grip && HasVisibleStyle(child))
			visible.push_back(child);
	}

	for (HWND child : visible) {
		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) == 0) {
			parts.trackbar = child;
			parts.trackbarRect = RectIn(child, row);
		}
	}
	if (!parts.trackbar)
		return parts; // sem barra nao e linha de slider

	// O nome e o controle de TEXTO mais a direita entre os que ficam a esquerda
	// da barra. Texto e nao botao: o modo de edicao poe botoes ali, e um botao
	// nao e o nome do slider.
	for (HWND child : visible) {
		if (child == parts.trackbar)
			continue;
		if (_wcsicmp(ClassOf(child).c_str(), L"Button") == 0)
			continue;
		const RECT rc = RectIn(child, row);
		if (rc.left >= parts.trackbarRect.left)
			continue; // a porcentagem, que fica depois da barra
		if (!parts.name || rc.left > parts.nameRect.left) {
			parts.name = child;
			parts.nameRect = rc;
		}
	}
	if (!parts.name)
		return parts;

	// A ancora e o controle que termina antes do nome, mais a direita deles.
	//
	// Funciona igual nos dois estados, com a alca posta ou nao, porque a caixa
	// de marcacao nunca se mexe: ela termina em 42 tanto com o nome em 47 quanto
	// com o nome em 61.
	int anchor = -1;
	for (HWND child : visible) {
		if (child == parts.trackbar || child == parts.name)
			continue;
		const RECT rc = RectIn(child, row);
		if (rc.right <= parts.nameRect.left && rc.right > anchor)
			anchor = static_cast<int>(rc.right);
	}
	parts.anchorRight = (anchor >= 0) ? anchor : static_cast<int>(parts.nameRect.left);

	for (HWND child : visible) {
		if (child == parts.trackbar)
			continue;
		const RECT rc = RectIn(child, row);
		if (rc.left < parts.nameRect.left || rc.left >= parts.trackbarRect.left)
			continue;
		parts.movable.push_back(child);
	}

	parts.ok = !parts.movable.empty();
	return parts;
}

std::wstring NameOf(HWND row) {
	const RowParts parts = FindParts(row);
	return parts.ok ? TextOf(parts.name) : std::wstring();
}

// As linhas visiveis de cima para baixo, com nome e altura.
//
// Ordenadas pela posicao na TELA e nao pela z-order: a z-order nao promete
// acompanhar a ordem em que as linhas aparecem, e e a da tela que o usuario esta
// arrastando. Depois do primeiro arrasto as duas ja divergem, porque a linha
// carregada vai para o topo da z-order.
std::vector<Row> FindRows(HWND host) {
	std::vector<Row> rows;
	for (HWND window : VisibleRowWindows(host)) {
		Row row;
		row.window = window;
		row.name = NameOf(window);
		row.top = static_cast<int>(RectIn(window, host).top);
		rows.push_back(row);
	}

	std::sort(rows.begin(), rows.end(),
			  [](const Row& a, const Row& b) { return a.top < b.top; });
	return rows;
}

std::vector<HWND> WindowsOf(const std::vector<Row>& rows) {
	std::vector<HWND> out;
	out.reserve(rows.size());
	for (const Row& row : rows)
		out.push_back(row.window);
	return out;
}

// A ordem completa por nome, escondidas incluidas.
//
// Reconstruida a partir da ordem persistida, e NAO das coordenadas: a posicao Y
// de uma linha escondida nao segue regra nenhuma -- ela fica parada onde estava
// enquanto as visiveis se compactam por cima, e as do viveiro estao todas
// empilhadas no mesmo pixel.
std::vector<std::wstring> FullNameOrder(HWND host) {
	std::vector<std::wstring> present;
	for (HWND window : RowWindows(host)) {
		std::wstring name = NameOf(window);
		if (!name.empty())
			present.push_back(std::move(name));
	}

	std::vector<std::wstring> out;
	out.reserve(present.size());
	for (int index : ApplyDesiredOrder(g_desiredOrder, present))
		out.push_back(present[static_cast<size_t>(index)]);
	return out;
}

bool BeginDrag(HWND row);
void DragToCursor();
void FinishDrag(bool cancelled);
void RefreshRows();

// A troca de outfit pode destruir a linha ou o painel enquanto uma alca ainda
// tem a captura do mouse. Nesse caso nao ha mais onde recolocar as linhas: o
// arrasto apenas deixa de existir junto com a arvore antiga.
void AbandonDrag() {
	if (!g_drag.active)
		return;
	if (g_drag.slot && IsWindow(g_drag.slot))
		DestroyWindow(g_drag.slot);
	g_drag = Drag();
	LogF("reorder: arrasto abandonado porque a lista foi reconstruida");
}

// Se o ponteiro esta em cima desta alca.
//
// Guardado na propria janela, e nao num global: sao mais de cem alcas na tela e
// o realce e de UMA. Um global faria a lista inteira acender junto.
bool GripIsHot(HWND grip) {
	return GetWindowLongPtrW(grip, GWLP_USERDATA) != 0;
}

// Manda repintar a alca, e a faixa do PAI debaixo dela.
//
// As duas, porque a alca e WS_EX_TRANSPARENT: ela nao tem fundo proprio, entao
// quem apaga o desenho velho e a linha. Invalidar so a alca deixaria os pontos
// antigos por baixo dos novos.
void RepaintGrip(HWND grip) {
	HWND row = GetParent(grip);
	if (!row)
		return;
	const RECT rc = RectIn(grip, row);
	InvalidateRect(row, &rc, TRUE);
	InvalidateRect(grip, nullptr, FALSE);
}

void SetGripHot(HWND grip, bool hot) {
	if (GripIsHot(grip) == hot)
		return;
	SetWindowLongPtrW(grip, GWLP_USERDATA, hot ? 1 : 0);
	RepaintGrip(grip);
}

// Desenha os seis pontos da alca, e nada mais.
//
// Desenhados, e nao um caractere numa fonte. A versao anterior punha o simbolo
// "identico a" num Static, e o diagnostico provou que ele ficava na posicao
// certa e mesmo assim nada aparecia: um glifo depende de a fonte te-lo, do corpo
// dela e de como o controle o alinha. Seis retangulos nao dependem de nada
// disso.
//
// E sem fundo NENHUM. Uma versao anterior pedia o pincel ao pai por
// WM_CTLCOLORSTATIC, supondo que um painel do wx respondesse como um Static
// comum -- e o que voltava era o branco do DefWindowProc. Na tela isso virou um
// quadradinho branco no meio de uma linha escura. A alca e WS_EX_TRANSPARENT: o
// pai pinta o fundo dele por baixo, e aqui so ficam os pontos.
void PaintGrip(HWND grip, HDC dc) {
	RECT rc = {};
	GetClientRect(grip, &rc);

	// Cinza parado, azul sob o ponteiro e durante o arrasto. Um cinza discreto
	// e o que faz a alca ler como parte da tipografia da linha em vez de mais um
	// controle; o azul e o que confirma que ela e clicavel.
	const COLORREF tone = GripIsHot(grip)
							  ? GetSysColor(COLOR_HIGHLIGHT)
							  : (g_dark ? RGB(150, 150, 150) : GetSysColor(COLOR_GRAYTEXT));

	HBRUSH ink = CreateSolidBrush(tone);
	if (!ink)
		return;

	const int dot = 2;
	const int gapX = 2;
	const int gapY = 2;
	const int width = dot * 2 + gapX;
	const int height = dot * 3 + gapY * 2;
	const int originX = (static_cast<int>(rc.right) - width) / 2;
	const int originY = (static_cast<int>(rc.bottom) - height) / 2;

	for (int line = 0; line < 3; ++line) {
		for (int column = 0; column < 2; ++column) {
			RECT bit = {};
			bit.left = originX + column * (dot + gapX);
			bit.top = originY + line * (dot + gapY);
			bit.right = bit.left + dot;
			bit.bottom = bit.top + dot;
			FillRect(dc, &bit, ink);
		}
	}
	DeleteObject(ink);
}

// A alca e dona do gesto.
//
// Ela captura o mouse no aperto, entao o movimento e o soltar chegam AQUI mesmo
// que o ponteiro saia da linha, do painel ou da janela. A versao anterior
// dependia de o clique atravessar um controle por hit-test e de o hook global
// adivinhar em que janela ele tinha caido; e como o alvo real da mensagem era a
// alca e nao a linha, a linha nunca era encontrada.
LRESULT CALLBACK GripProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	switch (msg) {
		case WM_NCDESTROY:
			if (g_drag.active) {
				HWND row = GetParent(hwnd);
				if (std::find(g_drag.order.begin(), g_drag.order.end(), row) != g_drag.order.end())
					AbandonDrag();
			}
			RemoveWindowSubclass(hwnd, GripProc, id);
			break;

		case WM_ERASEBKGND:
			return 1; // o pai ja pintou o fundo: a alca e transparente

		case WM_PAINT: {
			PAINTSTRUCT paint = {};
			if (HDC dc = BeginPaint(hwnd, &paint))
				PaintGrip(hwnd, dc);
			EndPaint(hwnd, &paint);
			return 0;
		}

		// Seta vertical, e nao a de mover em quatro direcoes: a linha so anda
		// para cima e para baixo. A de quatro pontas prometia um movimento
		// lateral que nao existe.
		case WM_SETCURSOR:
			SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
			return TRUE;

		// So captura o mouse se o arrasto REALMENTE comecou.
		//
		// BeginDrag desiste em varios casos -- painel sumido, uma linha so, a
		// linha nao encontrada -- e capturar antes de saber disso prendia o
		// mouse para sempre: o WM_LBUTTONUP nao soltava, porque ele so solta
		// quando ha um arrasto para encerrar.
		case WM_LBUTTONDOWN:
			SetGripHot(hwnd, true);
			if (BeginDrag(GetParent(hwnd)))
				SetCapture(hwnd);
			return 0;

		case WM_MOUSEMOVE:
			if (g_drag.active) {
				DragToCursor();
				return 0;
			}
			if (!GripIsHot(hwnd)) {
				SetGripHot(hwnd, true);
				// Sem pedir o aviso de saida, a alca acenderia e nunca mais
				// apagaria: WM_MOUSELEAVE nao chega sozinho.
				TRACKMOUSEEVENT track = {};
				track.cbSize = sizeof(track);
				track.dwFlags = TME_LEAVE;
				track.hwndTrack = hwnd;
				TrackMouseEvent(&track);
			}
			return 0;

		case WM_MOUSELEAVE:
			if (!g_drag.active)
				SetGripHot(hwnd, false);
			return 0;

		case WM_LBUTTONUP:
			if (g_drag.active) {
				FinishDrag(false);
				ReleaseCapture();
			}
			SetGripHot(hwnd, false);
			return 0;

		// Perder a captura -- alt-tab, outra janela roubando -- encerra o
		// arrasto onde ele estiver. Sem isto o estado ficaria aberto e a lista
		// seguiria um mouse que ja nao esta arrastando nada.
		case WM_CAPTURECHANGED:
			if (g_drag.active)
				FinishDrag(false);
			return 0;

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// O vao: uma linha fina no lugar para onde a linha vai cair.
//
// Sem ele o arrasto e ambiguo. As outras linhas se acomodam e deixam um buraco,
// mas um buraco no meio de uma lista de barras cinzentas nao se le como "e aqui
// que ela entra" -- le-se como um erro de desenho.
//
// Uma LINHA, e nao uma moldura do tamanho da linha. A moldura desenhava uma
// caixa vazia onde vai entrar conteudo, o que parece ferramenta de depuracao; o
// tracinho horizontal e o que as listas arrastaveis usam, e diz a mesma coisa
// sem competir com o resto da tela.
LRESULT CALLBACK SlotProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	switch (msg) {
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, SlotProc, id);
			break;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT: {
			PAINTSTRUCT paint = {};
			HDC dc = BeginPaint(hwnd, &paint);
			if (dc) {
				RECT rc = {};
				GetClientRect(hwnd, &rc);

				// A janela continua ocupando a linha inteira -- e o espaco que a
				// linha arrastada vai reocupar -- mas so o meio dela e pintado.
				RECT line = rc;
				line.top = rc.top + (rc.bottom - rc.top) / 2 - 1;
				line.bottom = line.top + 2;
				line.left += 5;
				line.right -= 5;

				if (HBRUSH edge = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT))) {
					FillRect(dc, &line, edge);
					DeleteObject(edge);
				}
			}
			EndPaint(hwnd, &paint);
			return 0;
		}

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

bool MoveWindowTo(HWND window, HWND parent, int left, int top) {
	const RECT rc = RectIn(window, parent);
	if (rc.left == left && rc.top == top)
		return false;
	SetWindowPos(window, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	return true;
}

// Poe a linha no formato com alca, quantas vezes for chamada. Devolve se mexeu
// em alguma coisa.
//
// Idempotente de proposito: o wx refaz o layout da linha por conta propria e
// devolve tudo para as posicoes originais, entao esta funcao precisa distinguir
// "ainda nao mexi" de "ja esta como eu quero". Quem diz e a borda direita da
// alca: se ela encosta no nome, o layout esta feito; se nao encosta, o wx
// acabou de desfaze-lo.
//
// Uma versao que so somasse a largura a cada chamada empurraria o nome para fora
// da linha na segunda vez.
bool LayoutRow(HWND row) {
	HWND grip = GripOf(row);
	if (!grip)
		return false;

	const RowParts parts = FindParts(row);
	if (!parts.ok)
		return false;

	const RECT gripRect = RectIn(grip, row);
	const int nameHeight = static_cast<int>(parts.nameRect.bottom - parts.nameRect.top);

	if (gripRect.right == parts.nameRect.left) {
		// Ja esta como queremos. So mantem a alca acompanhando a altura da
		// linha, caso ela mude.
		if (gripRect.top == parts.nameRect.top && gripRect.bottom - gripRect.top == nameHeight)
			return false;
		SetWindowPos(grip, nullptr, static_cast<int>(gripRect.left),
					 static_cast<int>(parts.nameRect.top),
					 static_cast<int>(gripRect.right - gripRect.left), nameHeight,
					 SWP_NOZORDER | SWP_NOACTIVATE);
		return true;
	}

	// A alca vai da ancora ate onde o nome VAI ficar.
	//
	// Comecar na ancora e o que deixa a folga igual dos dois lados. Ancorada no
	// nome, como antes, a alca ficava colada nele e sobrava a folga inteira do
	// outro lado -- que foi o "muito espaco a esquerda e quase nada a direita"
	// que apareceu na tela.
	const int gripRight = static_cast<int>(parts.nameRect.left) + kGripWidth;
	const int gripLeft = (parts.anchorRight < gripRight) ? parts.anchorRight : gripRight - kGripWidth;

	SetWindowPos(grip, nullptr, gripLeft, static_cast<int>(parts.nameRect.top),
				 gripRight - gripLeft, nameHeight, SWP_NOZORDER | SWP_NOACTIVATE);

	// O nome e o que mais estiver entre ele e a barra andam para a direita. O
	// lapis, a caixa e a porcentagem ficam onde estavam, e por isso a barra
	// encolhe pelo mesmo tanto que anda -- senao passaria por cima da
	// porcentagem.
	for (HWND child : parts.movable) {
		const RECT rc = RectIn(child, row);
		MoveWindowTo(child, row, static_cast<int>(rc.left) + kGripWidth, static_cast<int>(rc.top));
	}

	SetWindowPos(parts.trackbar, nullptr, static_cast<int>(parts.trackbarRect.left) + kGripWidth,
				 static_cast<int>(parts.trackbarRect.top),
				 static_cast<int>(parts.trackbarRect.right - parts.trackbarRect.left) - kGripWidth,
				 static_cast<int>(parts.trackbarRect.bottom - parts.trackbarRect.top),
				 SWP_NOZORDER | SWP_NOACTIVATE);
	return true;
}

// Poe a alca na linha, se ela ainda nao tem uma. Devolve se mexeu em alguma
// coisa.
//
// A janela e criada ANTES de qualquer controle sair do lugar. Na ordem contraria
// -- que era a de antes -- uma falha na criacao deixava a linha deslocada e sem
// alca nenhuma para explicar por que.
bool AddGrip(HWND row) {
	if (GripOf(row))
		return LayoutRow(row);

	const RowParts parts = FindParts(row);
	if (!parts.ok)
		return false;

	// WS_EX_TRANSPARENT: o pai pinta o fundo por baixo, e a alca so poe os
	// pontos em cima. E o que tira o quadradinho branco sem precisar adivinhar a
	// cor de fundo da linha.
	HWND grip = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"",
								WS_CHILD | WS_VISIBLE | SS_NOTIFY,
								static_cast<int>(parts.nameRect.left),
								static_cast<int>(parts.nameRect.top), kGripWidth,
								static_cast<int>(parts.nameRect.bottom - parts.nameRect.top), row,
								reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGripId)),
								reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(row, GWLP_HINSTANCE)),
								nullptr);
	if (!grip)
		return false; // a linha continua intacta

	SetWindowSubclass(grip, GripProc, kGripSubclassId, 0);
	LayoutRow(row);
	return true;
}

bool RemoveGrip(HWND row) {
	HWND grip = GripOf(row);
	if (!grip)
		return false;

	const RowParts parts = FindParts(row);
	const RECT gripRect = RectIn(grip, row);
	const bool shifted = parts.ok && gripRect.right == parts.nameRect.left;

	DestroyWindow(grip);
	if (!shifted)
		return true;

	for (HWND child : parts.movable) {
		const RECT rc = RectIn(child, row);
		MoveWindowTo(child, row, static_cast<int>(rc.left) - kGripWidth, static_cast<int>(rc.top));
	}

	SetWindowPos(parts.trackbar, nullptr, static_cast<int>(parts.trackbarRect.left) - kGripWidth,
				 static_cast<int>(parts.trackbarRect.top),
				 static_cast<int>(parts.trackbarRect.right - parts.trackbarRect.left) + kGripWidth,
				 static_cast<int>(parts.trackbarRect.bottom - parts.trackbarRect.top),
				 SWP_NOZORDER | SWP_NOACTIVATE);
	return true;
}

bool PlaceRow(HWND host, HWND row, int top) {
	const RECT rc = RectIn(row, host);
	return MoveWindowTo(row, host, static_cast<int>(rc.left), top);
}

// Poe a lista na ordem que o usuario escolheu. Devolve se mexeu em alguma coisa.
bool ApplyOrder(HWND host, const std::vector<Row>& rows) {
	if (rows.empty() || g_desiredOrder.empty())
		return false;

	std::vector<std::wstring> present;
	present.reserve(rows.size());
	for (const Row& row : rows)
		present.push_back(row.name);

	const std::vector<int> arrangement = ApplyDesiredOrder(g_desiredOrder, present);
	if (arrangement.size() != rows.size())
		return false;

	// Os lugares sao os que a lista ja tem; so muda quem ocupa cada um. Assim a
	// altura de cada linha e o espacamento continuam sendo os do wx.
	std::vector<int> slots;
	slots.reserve(rows.size());
	for (const Row& row : rows)
		slots.push_back(row.top);
	std::sort(slots.begin(), slots.end());

	bool moved = false;
	for (size_t i = 0; i < arrangement.size(); ++i) {
		const int from = arrangement[i];
		if (from >= 0 && from < static_cast<int>(rows.size()))
			moved = PlaceRow(host, rows[static_cast<size_t>(from)].window, slots[i]) || moved;
	}
	return moved;
}

// Poe a lista em dia: alcas onde precisa, ordem escolhida aplicada.
//
// Chamada de todo lado e com frequencia -- e ela quem conserta o que o wx
// desfez. Por isso ela so redesenha e so registra quando REALMENTE mexeu em
// alguma coisa: chamada a cada movimento de mouse, redesenhar sempre faria a
// lista piscar sem parar.
void RefreshRows() {
	if (g_drag.active)
		return;

	HWND host = PickSliderHost(g_frame, g_posePanel);
	if (!host)
		return;

	const std::vector<Row> rows = FindRows(host);
	if (rows.empty())
		return;

	bool changed = false;
	for (const Row& row : rows)
		changed = (g_gripsOn ? AddGrip(row.window) : RemoveGrip(row.window)) || changed;

	// A ordem escolhida e reaplicada AQUI, e nao so no fim do arrasto: este e o
	// momento em que a lista acabou de ser refeita pelo programa, e sem isto a
	// escolha do usuario se perderia na primeira troca de outfit.
	changed = ApplyOrder(host, rows) || changed;

	if (!changed)
		return;

	RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
	LogF("reorder: %d linhas visiveis de %d no painel, layout reaplicado",
		 static_cast<int>(rows.size()), static_cast<int>(RowWindows(host).size()));
}

// O painel avisa quando muda.
//
// Duas coisas interessam. WM_PARENTNOTIFY conta que uma linha nasceu ou morreu,
// que e a troca de outfit ou de preset. WM_SIZE conta que o wx acabou de refazer
// o layout, o que desmancha tanto o deslocamento dentro das linhas quanto a
// ordem delas.
//
// Nos dois casos o trabalho e ADIADO por PostMessage. Mexer nos filhos no meio
// da passagem de layout do wx e pedir para brigar com ela; deixado para a
// proxima mensagem, o wx ja terminou e o campo esta livre.
LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == kRecheckMsg) {
		g_recheckPending = false;
		RefreshRows();
		return 0;
	}

	if (msg == WM_NCDESTROY) {
		if (g_drag.active && g_drag.host == hwnd)
			AbandonDrag();
		RemoveWindowSubclass(hwnd, HostProc, id);
		if (hwnd == g_knownHost)
			g_knownHost = nullptr;
	}

	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

	if ((msg == WM_SIZE || msg == WM_PARENTNOTIFY) && !g_recheckPending && !g_drag.active) {
		g_recheckPending = true;
		PostMessageW(hwnd, kRecheckMsg, 0, 0);
	}
	return result;
}

void WatchHost(HWND host) {
	SetWindowSubclass(host, HostProc, kHostSubclassId, 0);
	LogF("reorder: painel %p sob observacao", static_cast<void*>(host));
}

// Passa os olhos na lista, de tempos em tempos.
//
// E uma varredura e nao uma comparacao, porque as coisas que desfazem o layout
// nao avisam ninguem. Entrar no modo de edicao troca quais controles a linha
// mostra sem criar nem destruir janela alguma, e sem redimensionar o painel:
// nenhuma mensagem chega ao HostProc. Digitar no filtro de sliders so acende e
// apaga linhas, e tambem nao muda o conjunto de janelas. Comparar conjuntos,
// que era o que esta funcao fazia, nao via nenhum dos dois acontecer.
//
// Sai barato porque RefreshRows so age quando encontra algo fora do lugar.
void RecheckOnMouseMove() {
	if (g_drag.active)
		return;

	const DWORD now = GetTickCount();
	if (now - g_lastCheck < 300)
		return;
	g_lastCheck = now;

	if (!g_knownHost || !IsWindow(g_knownHost)) {
		HWND host = PickSliderHost(g_frame, g_posePanel);
		if (!host)
			return;
		WatchHost(host);

		// Guardado AQUI, e nao dentro do RefreshRows.
		//
		// Guardado la, um painel que ainda nao tivesse linha visivel nenhuma
		// fazia RefreshRows desistir cedo, o painel nunca era dado por
		// encontrado, e isto voltava a subclassa-lo a cada trezentos
		// milissegundos -- o que o log mostrou acontecendo.
		g_knownHost = host;
	}

	RefreshRows();
}

void ShowSlotAt(int index) {
	if (!g_drag.slot || index < 0 || index >= static_cast<int>(g_drag.slotTops.size()))
		return;

	// Onde ele estava, para mandar o painel apagar aquele pedaco. Sem isto o
	// tracinho deixaria rastro: a janela e transparente e nao tem fundo proprio
	// para cobrir o desenho anterior.
	const RECT before = RectIn(g_drag.slot, g_drag.host);

	RECT client = {};
	GetClientRect(g_drag.host, &client);
	SetWindowPos(g_drag.slot, nullptr, 0, g_drag.slotTops[static_cast<size_t>(index)],
				 static_cast<int>(client.right), g_drag.rowHeight,
				 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

	InvalidateRect(g_drag.host, &before, TRUE);
	InvalidateRect(g_drag.slot, nullptr, FALSE);
}

bool BeginDrag(HWND row) {
	HWND host = GetParent(row);
	if (!host)
		return false;

	const std::vector<Row> rows = FindRows(host);
	if (rows.size() < 2)
		return false;

	int index = -1;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (rows[i].window == row)
			index = static_cast<int>(i);
	}
	if (index < 0)
		return false;

	const RECT rowRect = RectIn(row, host);

	g_drag = Drag();
	g_drag.active = true;
	g_drag.host = host;
	g_drag.order = WindowsOf(rows);
	g_drag.original = g_drag.order;
	for (const Row& entry : rows)
		g_drag.slotTops.push_back(entry.top);
	g_drag.rowHeight = static_cast<int>(rowRect.bottom - rowRect.top);
	g_drag.index = index;

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(host, &cursor);
	g_drag.grabOffset = static_cast<int>(cursor.y) - g_drag.slotTops[static_cast<size_t>(index)];

	g_drag.slot = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, host,
								  nullptr,
								  reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(host, GWLP_HINSTANCE)),
								  nullptr);
	if (g_drag.slot) {
		SetWindowSubclass(g_drag.slot, SlotProc, kSlotSubclassId, 0);
		ShowSlotAt(index);
	}

	// A linha na mao vai para o topo da z-order, senao ela passa POR TRAS das
	// vizinhas e do vao enquanto e arrastada, e parece ter sumido.
	SetWindowPos(row, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	LogF("reorder: peguei a linha %d de %d", index + 1, static_cast<int>(rows.size()));
	return true;
}

void DragToCursor() {
	if (!g_drag.active || !g_drag.host)
		return;

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(g_drag.host, &cursor);
	const int carried = static_cast<int>(cursor.y) - g_drag.grabOffset;

	const int wanted = SlotAt(g_drag.slotTops, carried);
	if (wanted >= 0 && wanted != g_drag.index) {
		MoveInOrder(g_drag.order, g_drag.index, wanted);
		g_drag.index = wanted;

		// As outras acomodam nos lugares; a da mao fica de fora, seguindo o
		// cursor -- e o que faz parecer que ela esta sendo carregada. O lugar
		// que sobra e o do vao.
		for (size_t i = 0; i < g_drag.order.size() && i < g_drag.slotTops.size(); ++i) {
			if (static_cast<int>(i) != g_drag.index)
				PlaceRow(g_drag.host, g_drag.order[i], g_drag.slotTops[i]);
		}
		ShowSlotAt(g_drag.index);
	}

	PlaceRow(g_drag.host, g_drag.order[static_cast<size_t>(g_drag.index)], carried);
}

void FinishDrag(bool cancelled) {
	if (!g_drag.active)
		return;

	g_drag.active = false;

	if (g_drag.slot) {
		DestroyWindow(g_drag.slot);
		g_drag.slot = nullptr;
	}

	if (cancelled)
		g_drag.order = g_drag.original;

	for (size_t i = 0; i < g_drag.order.size() && i < g_drag.slotTops.size(); ++i)
		PlaceRow(g_drag.host, g_drag.order[i], g_drag.slotTops[i]);

	// A escolha e guardada por NOME, que e o que sobrevive a lista ser refeita.
	//
	// E e COSTURADA na ordem completa, nao posta no lugar dela: com o filtro de
	// sliders ligado o arrasto so viu as linhas que sobraram na tela, e trocar a
	// ordem inteira pela desse punhado mandaria todas as escondidas para o fim
	// assim que a busca fosse limpa.
	if (!cancelled) {
		std::vector<std::wstring> subsetOrder;
		for (HWND row : g_drag.order) {
			std::wstring name = NameOf(row);
			if (!name.empty())
				subsetOrder.push_back(std::move(name));
		}
		g_desiredOrder = SpliceOrder(FullNameOrder(g_drag.host), subsetOrder);
		g_orderDirty = true;
		EnableSaveForOrder();
		MarkTitleForOrder();
	}

	if (g_drag.host && IsWindow(g_drag.host))
		RedrawWindow(g_drag.host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);

	LogF("reorder: arrasto %s, %d nomes na ordem guardada",
		 cancelled ? "cancelado" : "concluido", static_cast<int>(g_desiredOrder.size()));

	g_drag = Drag();
}

void OnGripsToggled(bool checked) {
	g_gripsOn = checked;
	RefreshRows();
	LogF("reorder: alcas %s pelo menu", checked ? "ligadas" : "desligadas");
}

bool PromptToSaveOrder(HWND hwnd) {
	if (!g_orderDirty)
		return true;

	const int answer = MessageBoxW(hwnd,
		L"The slider order has unsaved changes. Would you like to save them now?",
		L"Unsaved Changes", MB_YESNOCANCEL | MB_ICONWARNING);
	if (answer == IDCANCEL)
		return false;
	if (answer == IDYES) {
		const UINT saveCommand = g_saveId ? g_saveId : g_saveAsId;
		if (saveCommand)
			SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(saveCommand, 0), 0);
		return !g_orderDirty; // Save cancelado, falhou ou nao encontrou o .osp
	}

	g_orderDirty = false;
	ClearOrderTitleMark();
	return true;
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
						   UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, FrameProc, id);
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	const UINT command = (msg == WM_COMMAND) ? LOWORD(wParam) : 0;
	const bool closeCommand = (msg == WM_CLOSE) ||
		(msg == WM_SYSCOMMAND && ((wParam & 0xFFF0u) == SC_CLOSE)) ||
		(command != 0 && command == g_exitId);
	if (closeCommand && !PromptToSaveOrder(hwnd))
		return 0;

	const bool save = command != 0 && (command == g_saveId || command == g_saveAsId);
	OspSnapshot before;
	if (save && !g_desiredOrder.empty())
		before = SnapshotOspFiles();

	// O handler nativo precisa salvar primeiro. Save As inclusive mantem o
	// dialogo modal dentro desta chamada; quando ela volta o arquivo final ja
	// existe e e seguro identifica-lo pela mudanca no snapshot.
	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
	if (save && !g_desiredOrder.empty()) {
		PersistOrderAfterSave(before);
		if (g_orderDirty)
			EnableSaveForOrder(); // cancelamento ou recusa: continua salvavel
	}
	return result;
}

} // namespace

int SlotAt(const std::vector<int>& slotTops, int y) {
	if (slotTops.empty())
		return -1;

	// Antes do primeiro lugar e depois do ultimo caem nas pontas, e nao em
	// "nenhum": arrastar para fora da lista quer dizer levar a linha para o topo
	// ou para o fim, que e o que o usuario esta pedindo ao fazer isso.
	if (y <= slotTops.front())
		return 0;
	if (y >= slotTops.back())
		return static_cast<int>(slotTops.size()) - 1;

	for (size_t i = 0; i + 1 < slotTops.size(); ++i) {
		const int middle = slotTops[i] + (slotTops[i + 1] - slotTops[i]) / 2;
		if (y < middle)
			return static_cast<int>(i);
	}
	return static_cast<int>(slotTops.size()) - 1;
}

void MoveInOrder(std::vector<HWND>& order, int from, int to) {
	const int count = static_cast<int>(order.size());
	if (from < 0 || to < 0 || from >= count || to >= count || from == to)
		return;

	HWND moved = order[static_cast<size_t>(from)];
	order.erase(order.begin() + from);
	order.insert(order.begin() + to, moved);
}

std::vector<int> ApplyDesiredOrder(const std::vector<std::wstring>& desired,
								   const std::vector<std::wstring>& present) {
	std::vector<int> out;
	out.reserve(present.size());

	std::vector<bool> used(present.size(), false);

	// Primeiro os que o usuario ordenou, na ordem dele. Nome repetido consome um
	// slider de cada vez, e nome que sumiu simplesmente nao entra.
	for (const std::wstring& name : desired) {
		for (size_t i = 0; i < present.size(); ++i) {
			if (used[i] || present[i] != name)
				continue;
			used[i] = true;
			out.push_back(static_cast<int>(i));
			break;
		}
	}

	// Depois o que apareceu e ele nunca ordenou -- outro outfit, outro projeto --
	// na ordem em que o programa os deu. Descarta-los seria sumir com slider.
	for (size_t i = 0; i < present.size(); ++i) {
		if (!used[i])
			out.push_back(static_cast<int>(i));
	}

	return out;
}

std::vector<std::wstring> SpliceOrder(const std::vector<std::wstring>& full,
									  const std::vector<std::wstring>& subsetOrder) {
	// Quais lugares da ordem completa pertencem ao subconjunto.
	//
	// Casados um a um e da esquerda para a direita, e nao por "este nome esta na
	// lista?": dois sliders de mesmo nome marcariam os dois lugares mesmo que so
	// um deles estivesse na tela, e o resultado deixaria de ser uma permutacao.
	std::vector<bool> claimed(subsetOrder.size(), false);
	std::vector<bool> isSlot(full.size(), false);

	for (size_t i = 0; i < full.size(); ++i) {
		for (size_t j = 0; j < subsetOrder.size(); ++j) {
			if (claimed[j] || subsetOrder[j] != full[i])
				continue;
			claimed[j] = true;
			isSlot[i] = true;
			break;
		}
	}

	// So os nomes que acharam lugar entram no preenchimento.
	//
	// Um nome do subconjunto que nao esta na ordem completa nao reclamou lugar
	// nenhum, e enfia-lo mesmo assim expulsaria alguem: ele entraria num lugar
	// que pertence a outro, e esse outro sumiria da lista.
	std::vector<std::wstring> usable;
	for (size_t j = 0; j < subsetOrder.size(); ++j) {
		if (claimed[j])
			usable.push_back(subsetOrder[j]);
	}

	// Os lugares marcados recebem o subconjunto na ordem nova; os outros ficam
	// com quem ja estava neles.
	std::vector<std::wstring> out;
	out.reserve(full.size());
	size_t next = 0;
	for (size_t i = 0; i < full.size(); ++i) {
		if (isSlot[i] && next < usable.size())
			out.push_back(usable[next++]);
		else
			out.push_back(full[i]);
	}
	return out;
}

namespace SliderReorder {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	const PosePanel pose = FindPosePanel(frame);
	g_posePanel = pose.ok ? pose.panel : nullptr;
	g_gripsOn = Cfg().sliderDragHandles;
	g_dark = DetectAppearance(AppDir()) == Appearance::Dark;
	g_orderDirty = false;
	g_titleMarked = false;

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	if (HMENU bar = GetMenu(frame)) {
		const MenuTrail save = ResolveMenuTrail(xrc.c_str(), "fileSave");
		const MenuTrail saveAs = ResolveMenuTrail(xrc.c_str(), "fileSaveAs");
		const MenuTrail exit = ResolveMenuTrail(xrc.c_str(), "fileExit");
		g_saveId = save.empty() ? 0 : CommandIdAtLabeledPath(bar, save.path, save.labels);
		g_saveAsId = saveAs.empty() ? 0 : CommandIdAtLabeledPath(bar, saveAs.path, saveAs.labels);
		g_exitId = exit.empty() ? 0 : CommandIdAtLabeledPath(bar, exit.path, exit.labels);
	}
	if (!SetWindowSubclass(frame, FrameProc, kFrameSubclassId, 0))
		LogF("reorder: nao consegui observar Save/Save As");

	if (HMENU view = MenuToggle::FindMenu(frame, "menuView"))
		MenuToggle::Add(frame, view, L"Slider drag handles", g_gripsOn, OnGripsToggled);

	g_installed = true;

	// O painel ainda nao existe nesta altura -- ele nasce com o primeiro outfit
	// carregado. Quem o acha e a varredura no movimento do mouse.
	LogF("reorder: pronto, alcas %s", g_gripsOn ? "ligadas" : "desligadas");
	return true;
}

void Uninstall() {
	if (g_drag.active)
		FinishDrag(true);

	// As alcas saem ANTES dos ponteiros.
	//
	// Cada alca e uma janela de verdade, com um subclass nosso, e o nome e a
	// barra de cada linha estao deslocados por causa dela. Zerar os globais e ir
	// embora deixava as duas coisas para tras: janelas vivas chamando codigo
	// deste modulo, e linhas empurradas com uma folga que ja nao tem nada dentro
	// para explicar por que.
	//
	// Passar por TODAS as linhas, e nao so pelas visiveis: uma linha escondida
	// pelo filtro pode ter ganho a alca antes de sumir, e ela precisa voltar ao
	// tamanho certo do mesmo jeito. O bit de visibilidade que importa e o da
	// linha; os filhos dela mantem os deles, entao a medida continua valendo.
	if (g_knownHost && IsWindow(g_knownHost)) {
		for (HWND window : RowWindows(g_knownHost))
			RemoveGrip(window);
		RemoveWindowSubclass(g_knownHost, HostProc, kHostSubclassId);
	}
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameProc, kFrameSubclassId);

	g_frame = nullptr;
	g_posePanel = nullptr;
	g_installed = false;
	g_knownHost = nullptr;
	g_recheckPending = false;
	g_saveId = 0;
	g_saveAsId = 0;
	g_exitId = 0;
	g_orderDirty = false;
	g_titleMarked = false;
	g_drag = Drag();
}

bool HandleMouseMessage(MSG* msg) {
	if (!g_installed || !msg)
		return false;

	// O gesto pertence a alca, que capturou o mouse. Aqui sobra o Esc, que ela
	// nao recebe por nao ter foco de teclado, e a varredura barata da lista.
	if (msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE && g_drag.active) {
		FinishDrag(true);
		ReleaseCapture();
		return true;
	}

	if (msg->message == WM_MOUSEMOVE)
		RecheckOnMouseMove();

	return false;
}

} // namespace SliderReorder
