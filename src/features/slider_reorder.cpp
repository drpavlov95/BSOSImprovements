#include "features/slider_reorder.h"

#include <commctrl.h>

#include <algorithm>

#include "core/host.h"
#include "core/log.h"
#include "features/pose_panel.h"
#include "features/zero_sliders.h" // PickSliderHost
#include "win32/menu_toggle.h"
#include "win32/winfind.h"

namespace {

HWND g_frame = nullptr;
HWND g_posePanel = nullptr;
bool g_installed = false;

// A ordem em que as linhas estao AGORA na tela, e os lugares que elas ocupam.
//
// Os lugares sao lidos no inicio de cada arrasto, e nao guardados da
// instalacao: a lista e refeita a cada troca de outfit, e um lugar guardado de
// antes apontaria para uma linha que ja nao existe.
std::vector<HWND> g_order;
std::vector<int> g_slotTops;
std::vector<HWND> g_originalOrder;

bool g_dragging = false;
int g_dragIndex = -1;
int g_grabOffset = 0; // do topo da linha ate onde o usuario a pegou
HWND g_host = nullptr;

int TopOf(HWND window, HWND parent) {
	RECT rc = {};
	GetWindowRect(window, &rc);
	MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
	return static_cast<int>(rc.top);
}

// As linhas do painel: filhos diretos que tem uma barra dentro.
//
// "Tem uma barra dentro" e o que distingue linha de slider de qualquer outra
// coisa que o painel carregue -- cabecalho de categoria, rodape, separador.
std::vector<HWND> FindRows(HWND host) {
	std::vector<HWND> rows;
	for (HWND child : ChildrenOf(host)) {
		if (!FindDescendantsByClass(child, TRACKBAR_CLASSW).empty())
			rows.push_back(child);
	}

	std::sort(rows.begin(), rows.end(), [host](HWND a, HWND b) {
		return TopOf(a, host) < TopOf(b, host);
	});
	return rows;
}

// Quanto os controles do programa andam para a direita para abrir espaco.
//
// A medida veio da propria linha, registrada no log: 766x25, com o lapis em
// 0..22, a caixa em 27..42, o nome em 47..149, a barra em 154..715 e o valor em
// 722..762. Nao havia UM pixel livre -- o maior vao eram cinco pixels entre o
// lapis e a caixa -- entao a alca so cabe empurrando.
const int kHandleShift = 20;
const int kHandleId = 0xBF03;

bool g_handlesOn = true;
UINT g_handleMenuId = 0;
HWND g_handleAnchor = nullptr; // uma linha ja tratada, para notar a lista refeita

HWND HandleOf(HWND row) {
	return GetDlgItem(row, kHandleId);
}

// Poe a alca numa linha e empurra o resto para a direita.
//
// Quem tem a barra encolhe em vez de andar: se ela tambem andasse, o campo de
// valor sairia pela borda da linha.
void AddHandle(HWND row) {
	if (HandleOf(row))
		return; // ja tem

	const std::vector<HWND> children = ChildrenOf(row);
	if (children.empty())
		return;

	int trackbarLeft = 0;
	bool haveTrackbar = false;
	for (HWND child : children) {
		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) != 0)
			continue;
		RECT rc = {};
		GetWindowRect(child, &rc);
		MapWindowPoints(nullptr, row, reinterpret_cast<POINT*>(&rc), 2);
		trackbarLeft = static_cast<int>(rc.left);
		haveTrackbar = true;
	}
	if (!haveTrackbar)
		return; // sem barra nao e linha de slider

	for (HWND child : children) {
		RECT rc = {};
		GetWindowRect(child, &rc);
		MapWindowPoints(nullptr, row, reinterpret_cast<POINT*>(&rc), 2);

		const int width = static_cast<int>(rc.right - rc.left);
		const int height = static_cast<int>(rc.bottom - rc.top);

		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) == 0) {
			SetWindowPos(child, nullptr, static_cast<int>(rc.left) + kHandleShift,
						 static_cast<int>(rc.top), width - kHandleShift, height,
						 SWP_NOZORDER | SWP_NOACTIVATE);
		} else if (static_cast<int>(rc.left) < trackbarLeft) {
			SetWindowPos(child, nullptr, static_cast<int>(rc.left) + kHandleShift,
						 static_cast<int>(rc.top), 0, 0,
						 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// A direita da barra fica onde esta: o valor nao pode sair da linha.
	}

	RECT rowRect = {};
	GetClientRect(row, &rowRect);
	const int size = 16;
	const int top = (static_cast<int>(rowRect.bottom) - size) / 2;

	// Static, e nao Button, de proposito: Static sem SS_NOTIFY devolve o clique
	// ao pai, entao a alca pega o arrasto pelo mesmo caminho que o fundo da
	// linha ja usa. Um botao consumiria o clique e nao arrastaria nada.
	HWND handle = CreateWindowExW(0, L"STATIC", L"≡",
								  WS_CHILD | WS_VISIBLE | SS_CENTER,
								  2, top > 0 ? top : 0, size, size, row,
								  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kHandleId)),
								  reinterpret_cast<HINSTANCE>(
									  GetWindowLongPtrW(row, GWLP_HINSTANCE)),
								  nullptr);
	if (!handle)
		return;

	if (HFONT font = reinterpret_cast<HFONT>(SendMessageW(row, WM_GETFONT, 0, 0)))
		SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

// Tira a alca e devolve os controles ao lugar.
void RemoveHandle(HWND row) {
	HWND handle = HandleOf(row);
	if (!handle)
		return;

	DestroyWindow(handle);

	int trackbarLeft = 0;
	bool haveTrackbar = false;
	for (HWND child : ChildrenOf(row)) {
		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) != 0)
			continue;
		RECT rc = {};
		GetWindowRect(child, &rc);
		MapWindowPoints(nullptr, row, reinterpret_cast<POINT*>(&rc), 2);
		trackbarLeft = static_cast<int>(rc.left);
		haveTrackbar = true;
	}
	if (!haveTrackbar)
		return;

	for (HWND child : ChildrenOf(row)) {
		RECT rc = {};
		GetWindowRect(child, &rc);
		MapWindowPoints(nullptr, row, reinterpret_cast<POINT*>(&rc), 2);

		const int width = static_cast<int>(rc.right - rc.left);
		const int height = static_cast<int>(rc.bottom - rc.top);

		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) == 0) {
			SetWindowPos(child, nullptr, static_cast<int>(rc.left) - kHandleShift,
						 static_cast<int>(rc.top), width + kHandleShift, height,
						 SWP_NOZORDER | SWP_NOACTIVATE);
		} else if (static_cast<int>(rc.left) <= trackbarLeft) {
			SetWindowPos(child, nullptr, static_cast<int>(rc.left) - kHandleShift,
						 static_cast<int>(rc.top), 0, 0,
						 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
}

void PlaceRow(HWND row, int top) {
	RECT rc = {};
	GetWindowRect(row, &rc);
	MapWindowPoints(nullptr, g_host, reinterpret_cast<POINT*>(&rc), 2);

	SetWindowPos(row, nullptr, static_cast<int>(rc.left), top, 0, 0,
				 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Poe cada linha no lugar dela, menos a que esta na mao do usuario.
void LayOutExceptDragged() {
	for (size_t i = 0; i < g_order.size() && i < g_slotTops.size(); ++i) {
		if (static_cast<int>(i) == g_dragIndex)
			continue;
		PlaceRow(g_order[i], g_slotTops[i]);
	}
}

void Finish(bool cancelled) {
	if (!g_dragging)
		return;

	g_dragging = false;

	// Cancelar devolve a ordem que estava antes do arrasto, e nao so a linha
	// que estava na mao: as outras ja foram empurradas de lugar no caminho.
	if (cancelled && g_originalOrder.size() == g_slotTops.size()) {
		g_order = g_originalOrder;
		g_dragIndex = -1;
		for (size_t i = 0; i < g_order.size(); ++i)
			PlaceRow(g_order[i], g_slotTops[i]);
	} else if (g_dragIndex >= 0 && g_dragIndex < static_cast<int>(g_slotTops.size())) {
		PlaceRow(g_order[static_cast<size_t>(g_dragIndex)], g_slotTops[static_cast<size_t>(g_dragIndex)]);
	}

	if (g_host && IsWindow(g_host))
		RedrawWindow(g_host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);

	LogF("reorder: arrasto %s, linha terminou na posicao %d de %d",
		 cancelled ? "cancelado" : "concluido", g_dragIndex + 1,
		 static_cast<int>(g_order.size()));

	g_dragIndex = -1;
}

// Aplica ou tira as alcas de todas as linhas do painel de agora.
void RefreshHandles() {
	HWND host = PickSliderHost(g_frame, g_posePanel);
	if (!host)
		return;

	const std::vector<HWND> rows = FindRows(host);
	if (rows.empty())
		return;

	for (HWND row : rows) {
		if (g_handlesOn)
			AddHandle(row);
		else
			RemoveHandle(row);
	}

	g_handleAnchor = g_handlesOn ? rows.front() : nullptr;
	LogF("reorder: alcas %s em %d linhas", g_handlesOn ? "postas" : "tiradas",
		 static_cast<int>(rows.size()));
}

// A lista e refeita a cada troca de outfit, e as alcas morrem com as linhas
// velhas. Uma checagem de uma chamada por movimento de mouse basta para notar.
void RefreshHandlesIfListWasRebuilt() {
	if (!g_handlesOn)
		return;
	if (g_handleAnchor && IsWindow(g_handleAnchor) && HandleOf(g_handleAnchor))
		return;
	RefreshHandles();
}

void OnHandlesToggled(bool checked) {
	g_handlesOn = checked;
	RefreshHandles();
}

bool BeginDrag(MSG* msg) {
	// So o FUNDO da linha pega. Clique em controle -- lapis, caixa, barra --
	// chega com hwnd do controle, e ali o arrasto nao pode roubar nada.
	HWND host = PickSliderHost(g_frame, g_posePanel);
	if (!host)
		return false;

	const std::vector<HWND> rows = FindRows(host);
	int index = -1;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (rows[i] == msg->hwnd)
			index = static_cast<int>(i);
	}

	if (index < 0) {
		// Clique dentro do painel que NAO caiu numa linha.
		//
		// E a diferenca entre "o clique foi num controle, e esta certo passar"
		// e "a area de pega nao existe onde eu achei que existia". As duas se
		// parecem iguais na tela -- nada acontece -- e sem esta linha a segunda
		// nao teria como ser diagnosticada.
		for (HWND walk = msg->hwnd; walk; walk = GetParent(walk)) {
			if (walk != host)
				continue;
			LogF("reorder: clique em %p (classe '%ls') dentro do painel, mas fora de linha -- %d linhas conhecidas",
				 static_cast<void*>(msg->hwnd), ClassOf(msg->hwnd).c_str(),
				 static_cast<int>(rows.size()));
			break;
		}
		return false;
	}
	if (rows.size() < 2)
		return false; // uma linha so nao tem para onde ir

	g_host = host;
	g_order = rows;
	g_originalOrder = rows;
	g_slotTops.clear();
	g_slotTops.reserve(rows.size());
	for (HWND row : rows)
		g_slotTops.push_back(TopOf(row, host));

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(host, &cursor);

	g_dragging = true;
	g_dragIndex = index;
	g_grabOffset = static_cast<int>(cursor.y) - g_slotTops[static_cast<size_t>(index)];

	LogF("reorder: peguei a linha %d de %d", index + 1, static_cast<int>(rows.size()));

	// A geometria de UMA linha, uma unica vez por sessao.
	//
	// E o que falta para saber se cabe um botao de arrastar ao lado do lapis e
	// da caixa, ou se seria preciso empurrar os controles do programa para
	// abrir espaco. Sem a medida, decidir isso seria adivinhar de novo -- e
	// adivinhar geometria ja custou tres versoes da busca do symmetrize.
	static bool measured = false;
	if (!measured) {
		measured = true;
		RECT rowRect = {};
		GetWindowRect(msg->hwnd, &rowRect);
		LogF("reorder: linha %ldx%ld, filhos:", rowRect.right - rowRect.left,
			 rowRect.bottom - rowRect.top);

		for (HWND child : ChildrenOf(msg->hwnd)) {
			RECT rc = {};
			GetWindowRect(child, &rc);
			MapWindowPoints(nullptr, msg->hwnd, reinterpret_cast<POINT*>(&rc), 2);
			LogF("  %ls em (%ld,%ld) %ldx%ld", ClassOf(child).c_str(), rc.left, rc.top,
				 rc.right - rc.left, rc.bottom - rc.top);
		}
	}
	return true;
}

void DragTo(MSG*) {
	if (!g_dragging || !g_host)
		return;

	// Soltar fora da janela nao entrega o WM_LBUTTONUP aqui. Sem isto o
	// arrasto ficaria aberto e a lista continuaria seguindo o mouse solto.
	if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
		Finish(false);
		return;
	}

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(g_host, &cursor);

	const int wanted = SlotAt(g_slotTops, static_cast<int>(cursor.y) - g_grabOffset);
	if (wanted >= 0 && wanted != g_dragIndex) {
		MoveInOrder(g_order, g_dragIndex, wanted);
		g_dragIndex = wanted;
		LayOutExceptDragged();
	}

	// A linha na mao segue o cursor, e nao o lugar: e o que da a sensacao de
	// estar carregando alguma coisa em vez de ver a lista piscar.
	PlaceRow(g_order[static_cast<size_t>(g_dragIndex)], static_cast<int>(cursor.y) - g_grabOffset);
}

} // namespace

int SlotAt(const std::vector<int>& slotTops, int y) {
	if (slotTops.empty())
		return -1;

	// Antes do primeiro lugar e depois do ultimo caem nas pontas, e nao em
	// "nenhum": arrastar para fora da lista tem que levar a linha para o topo
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

namespace SliderReorder {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	const PosePanel pose = FindPosePanel(frame);
	g_posePanel = pose.ok ? pose.panel : nullptr;
	g_handlesOn = Cfg().sliderDragHandles;

	if (HMENU view = MenuToggle::FindMenu(frame, "menuView")) {
		g_handleMenuId = MenuToggle::Add(frame, view, L"Slider drag handles",
										 g_handlesOn, OnHandlesToggled);
	}

	g_installed = true;
	LogF("reorder: pronto (arraste pela alca ou pelo fundo da linha), alcas %s",
		 g_handlesOn ? "ligadas" : "desligadas");
	return true;
}

void Uninstall() {
	Finish(true);
	g_frame = nullptr;
	g_posePanel = nullptr;
	g_host = nullptr;
	g_installed = false;
	g_handleAnchor = nullptr;
	g_handleMenuId = 0;
	g_order.clear();
	g_originalOrder.clear();
	g_slotTops.clear();
}

bool HandleMouseMessage(MSG* msg) {
	if (!g_installed || !msg)
		return false;

	switch (msg->message) {
		case WM_LBUTTONDOWN:
			return BeginDrag(msg);

		case WM_MOUSEMOVE:
			if (!g_dragging) {
				RefreshHandlesIfListWasRebuilt();
				return false;
			}
			DragTo(msg);
			return true;

		case WM_LBUTTONUP:
			if (!g_dragging)
				return false;
			Finish(false);
			return true;

		case WM_KEYDOWN:
			if (!g_dragging || msg->wParam != VK_ESCAPE)
				return false;
			Finish(true);
			return true;

		default:
			return false;
	}
}

} // namespace SliderReorder
