#include "features/slider_reorder.h"

#include <commctrl.h>

#include <algorithm>

#include "core/host.h"
#include "core/log.h"
#include "features/pose_panel.h"
#include "features/zero_sliders.h" // PickSliderHost
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

	g_installed = true;
	LogF("reorder: pronto (arraste pelo fundo da linha)");
	return true;
}

void Uninstall() {
	Finish(true);
	g_frame = nullptr;
	g_posePanel = nullptr;
	g_host = nullptr;
	g_installed = false;
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
			if (!g_dragging)
				return false;
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
