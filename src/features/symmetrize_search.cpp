#include "features/symmetrize_search.h"

#include <commctrl.h>

#include <algorithm>

#include "core/diag.h"
#include "core/host.h"
#include "core/log.h"
#include "core/theme.h"
#include "core/ui_thread.h"
#include "features/group_search.h" // MatchesFilter
#include "features/registry.h"
#include "win32/menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

const int kIdSearch = 0xBF02;
const UINT_PTR kFrameSubclassId = 0xB50F;
const UINT_PTR kPendingSubclassId = 0xB510;
const UINT_PTR kDialogSubclassId = 0xB511;
const UINT_PTR kEditSubclassId = 0xB512;
const UINT_PTR kScrollSubclassId = 0xB513;

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
HWND g_dialog = nullptr;
HWND g_scroll = nullptr;
HWND g_edit = nullptr;
bool g_dark = false;
UINT g_deferredLayout = 0;
RECT g_appliedScrollRect = {};
std::vector<AsymRow> g_rows;

// Ids dos dois comandos que abrem este dialogo. Sao a forma de reconhece-lo:
// enquanto um deles esta sendo tratado, o proximo dialogo criado e ele.
UINT g_maskSymVertId = 0;
UINT g_symVertId = 0;
bool g_expecting = false;

bool IsCheckBox(HWND control) {
	if (_wcsicmp(ClassOf(control).c_str(), L"Button") != 0)
		return false;
	const LONG type = GetWindowLongW(control, GWL_STYLE) & BS_TYPEMASK;
	return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX ||
		   type == BS_3STATE || type == BS_AUTO3STATE;
}

std::wstring TextOf(HWND control) {
	const int len = GetWindowTextLengthW(control);
	if (len <= 0 || len > 4096)
		return std::wstring();
	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	const int written = GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

RECT RectInParent(HWND control) {
	RECT rc = {};
	GetWindowRect(control, &rc);
	HWND parent = GetParent(control);
	if (parent)
		MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
	return rc;
}

int CountCheckBoxesUnder(HWND root, int depth) {
	if (depth > 8)
		return 0;
	int total = 0;
	for (HWND child : ChildrenOf(root)) {
		if (IsCheckBox(child))
			++total;
		total += CountCheckBoxesUnder(child, depth + 1);
	}
	return total;
}

// Um painel que contem linhas, e a que profundidade ele esta.
//
// A profundidade e o que separa cabecalho de conteudo: as linhas fixas moram
// direto na area que rola, e as de slider e de osso um nivel abaixo, cada
// grupo dentro do seu painel recolhivel.
struct RowHost {
	HWND window = nullptr;
	int depth = 0;
};

void CollectRowHosts(HWND root, std::vector<RowHost>& hosts, int depth) {
	if (depth > 8)
		return;
	bool hasCheck = false;
	for (HWND child : ChildrenOf(root)) {
		if (IsCheckBox(child))
			hasCheck = true;
		CollectRowHosts(child, hosts, depth + 1);
	}
	if (hasCheck)
		hosts.push_back({root, depth});
}

std::wstring SearchText() {
	if (!g_edit)
		return std::wstring();
	wchar_t raw[256] = {};
	GetWindowTextW(g_edit, raw, 256);
	return raw;
}

void CollectAllDescendants(HWND root, std::vector<HWND>& out, int depth) {
	if (depth > 8)
		return;
	for (HWND child : ChildrenOf(root)) {
		out.push_back(child);
		CollectAllDescendants(child, out, depth + 1);
	}
}

std::vector<HWND> AllDescendants(HWND root) {
	std::vector<HWND> out;
	CollectAllDescendants(root, out, 0);
	return out;
}

// Reposiciona as linhas de UM painel, fechando os buracos que o filtro abriu.
//
// A base -- o topo do primeiro lugar da lista -- e lida das posicoes ATUAIS, e
// nao guardada da instalacao. Guardar daria errado no primeiro rolar: a lista
// rola movendo os filhos, e as coordenadas de ontem apontariam para o lugar
// errado. Como linha escondida nunca e movida, o menor topo do painel continua
// sendo o do primeiro lugar, e da para reconstruir a regua a partir dele.
void ShrinkHostToFit(HWND host, const std::vector<AsymRow*>& rows, const std::vector<int>& placed);

void CompactHost(HWND host) {
	std::vector<AsymRow*> rows;
	for (AsymRow& row : g_rows) {
		if (row.host == host)
			rows.push_back(&row);
	}
	if (rows.empty())
		return;

	std::sort(rows.begin(), rows.end(),
			  [](const AsymRow* a, const AsymRow* b) { return a->top < b->top; });

	int base = 0;
	bool haveBase = false;
	for (const AsymRow* row : rows) {
		const RECT rc = RectInParent(row->check);
		if (!haveBase || rc.top < base) {
			base = static_cast<int>(rc.top);
			haveBase = true;
		}
	}
	if (!haveBase)
		return;

	std::vector<int> tops;
	std::vector<bool> visible;
	tops.reserve(rows.size());
	visible.reserve(rows.size());
	const int firstTop = rows.front()->top;
	for (const AsymRow* row : rows) {
		tops.push_back(base + (row->top - firstTop));
		visible.push_back(row->visible);
	}

	const std::vector<int> placed = CompactRowTops(tops, visible);
	if (placed.size() != rows.size())
		return;

	for (size_t i = 0; i < rows.size(); ++i) {
		if (!rows[i]->visible)
			continue;
		for (const AsymCell& cell : rows[i]->cells) {
			const RECT rc = RectInParent(cell.window);
			SetWindowPos(cell.window, nullptr, static_cast<int>(rc.left),
						 placed[i] + cell.offsetY, 0, 0,
						 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	ShrinkHostToFit(host, rows, placed);
}

// Encolhe o painel para caber so o que sobrou, e sobe o que vem depois dele.
//
// Compactar as linhas DENTRO do painel nao bastava: o painel continuava do
// tamanho de antes, e sobrava um vazio enorme embaixo dos resultados, com a
// secao seguinte empurrada para baixo dele.
//
// O que muda de altura e o painel que e filho DIRETO da area que rola -- o
// grupo recolhivel inteiro, e nao so a caixa interna onde as linhas moram.
// Encolher a de dentro nao move nada na tela.
void ShrinkHostToFit(HWND host, const std::vector<AsymRow*>& rows, const std::vector<int>& placed) {
	if (!g_scroll || rows.empty())
		return;

	// Sobe do painel das linhas ate o filho direto da area que rola.
	HWND wrapper = host;
	while (wrapper && GetParent(wrapper) != g_scroll) {
		HWND parent = GetParent(wrapper);
		if (!parent || parent == g_scroll)
			break;
		wrapper = parent;
	}
	if (!wrapper || GetParent(wrapper) != g_scroll)
		return; // o painel nao pendura na area que rola: nao mexe

	// Grupo recolhido pelo usuario: quem manda no tamanho e o wx.
	if (!HasVisibleStyle(host))
		return;

	// Onde termina a ultima linha que sobrou.
	int lastBottom = 0;
	bool any = false;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (!rows[i]->visible)
			continue;
		RECT rc = {};
		GetWindowRect(rows[i]->check, &rc);
		const int height = static_cast<int>(rc.bottom - rc.top);
		const int bottom = placed[i] + height;
		if (!any || bottom > lastBottom) {
			lastBottom = bottom;
			any = true;
		}
	}
	if (!any)
		lastBottom = 0;

	RECT wrapperRect = {};
	GetWindowRect(wrapper, &wrapperRect);
	MapWindowPoints(nullptr, g_scroll, reinterpret_cast<POINT*>(&wrapperRect), 2);

	RECT hostRect = {};
	GetWindowRect(host, &hostRect);
	MapWindowPoints(nullptr, g_scroll, reinterpret_cast<POINT*>(&hostRect), 2);

	// Onde o painel das linhas comeca DENTRO do envelope.
	//
	// O envelope carrega tambem o cabecalho do grupo -- o "Sliders" clicavel --
	// acima do painel. Ignorar essa faixa foi o defeito: eu media a ultima
	// linha em coordenadas do painel e aplicava o numero como altura do
	// envelope, entao com poucos resultados o envelope ficava menor que o
	// conteudo e cortava tudo. Era por isso que a lista sumia.
	const int paneTop = static_cast<int>(hostRect.top - wrapperRect.top);
	const int padding = 4;

	const int paneHeight = lastBottom + padding;
	const int wanted = paneTop + paneHeight;
	const int current = wrapperRect.bottom - wrapperRect.top;
	const int delta = wanted - current;
	if (delta == 0)
		return;

	// O painel e o envelope, nessa ordem: o de dentro primeiro, para o de fora
	// nunca ficar menor que o que carrega.
	SetWindowPos(host, nullptr, 0, 0, static_cast<int>(hostRect.right - hostRect.left),
				 paneHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(wrapper, nullptr, 0, 0, wrapperRect.right - wrapperRect.left, wanted,
				 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	// E tudo que estava abaixo dele sobe ou desce junto.
	for (HWND sibling : ChildrenOf(g_scroll)) {
		if (sibling == wrapper)
			continue;

		RECT rc = {};
		GetWindowRect(sibling, &rc);
		MapWindowPoints(nullptr, g_scroll, reinterpret_cast<POINT*>(&rc), 2);
		if (rc.top < wrapperRect.bottom)
			continue; // esta acima do painel: nao se mexe

		SetWindowPos(sibling, nullptr, static_cast<int>(rc.left),
					 static_cast<int>(rc.top) + delta, 0, 0,
					 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

// Reaprende a regua enquanto a lista ainda esta inteira na tela.
//
// A regua medida na instalacao envelhece. Os grupos "Sliders" e "Bones" comecam
// RECOLHIDOS, entao as linhas de cabecalho foram medidas coladas umas nas
// outras; ao expandir um grupo o programa afasta tudo, e a regua velha passava
// a puxar "26 bones" para cima, em cima dos sliders -- foi isso que apareceu
// empilhado na tela.
//
// Roda ANTES de esconder qualquer coisa: depois de esconder a tela ja nao e
// mais a regua. Enquanto nada esta escondido, ela e.
// Encolhe a faixa de rolagem ate o que sobrou de conteudo.
//
// Sem isto, filtrar deixa a lista curta mas a barra continua deixando rolar a
// altura toda de antes -- e o que o usuario ve embaixo dos resultados nao e
// espaco das linhas escondidas, e sim faixa de rolagem vazia. As linhas ja
// foram compactadas; o que faltava era contar ao Windows que o conteudo
// encolheu.
void ShrinkScrollRangeToContent() {
	if (!g_scroll || !IsWindow(g_scroll))
		return;

	int bottom = 0;
	for (HWND child : ChildrenOf(g_scroll)) {
		if (!HasVisibleStyle(child))
			continue;
		RECT rc = {};
		GetWindowRect(child, &rc);
		MapWindowPoints(nullptr, g_scroll, reinterpret_cast<POINT*>(&rc), 2);
		if (rc.bottom > bottom)
			bottom = static_cast<int>(rc.bottom);
	}
	if (bottom <= 0)
		return;

	RECT client = {};
	GetClientRect(g_scroll, &client);
	const int page = static_cast<int>(client.bottom - client.top);

	// O que a rolagem estava fazendo ANTES de mexermos nela.
	//
	// E o unico ponto cego que sobrou nesta feature: o filtro esconde certo e
	// as linhas compactam certo -- o log ja provou os dois -- mas se a barra
	// nao acompanhar, o usuario continua olhando para um vazio. Uma linha por
	// filtro diz se a faixa encolheu de verdade.
	SCROLLINFO before = {};
	before.cbSize = sizeof(before);
	before.fMask = SIF_ALL;
	GetScrollInfo(g_scroll, SB_VERT, &before);

	SCROLLINFO info = {};
	info.cbSize = sizeof(info);
	info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	info.nMin = 0;
	info.nMax = bottom;
	info.nPage = static_cast<UINT>(page > 0 ? page : 1);
	info.nPos = 0;
	SetScrollInfo(g_scroll, SB_VERT, &info, TRUE);

	SCROLLINFO after = {};
	after.cbSize = sizeof(after);
	after.fMask = SIF_ALL;
	GetScrollInfo(g_scroll, SB_VERT, &after);

	LogF("symmetrize: rolagem -- conteudo ate %d, janela %d | antes pos=%d max=%d pag=%u | depois pos=%d max=%d pag=%u",
		 bottom, page, before.nPos, before.nMax, before.nPage, after.nPos, after.nMax, after.nPage);
}

void RefreshRulers() {
	for (AsymRow& row : g_rows) {
		if (!row.visible)
			return; // ja filtrado: as posicoes atuais nao sao as naturais
	}
	for (AsymRow& row : g_rows)
		row.top = static_cast<int>(RectInParent(row.check).top);
}

void ApplyFilter() {
	if (g_rows.empty())
		return;

	RefreshRulers();

	// Volta ao topo ANTES de medir qualquer coisa.
	//
	// Duas razoes. A lista rolada era o que deixava o usuario olhando para um
	// vazio: os resultados estavam la em cima, fora da vista, e embaixo so
	// sobrava faixa de rolagem. E a regua so faz sentido com o conteudo na
	// origem -- rolar move os filhos, e medir no meio da rolagem daria
	// posicoes que nao valem depois.
	SendMessageW(g_scroll, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);

	const std::wstring query = SearchText();
	int shown = 0;
	int touched = 0;

	// Sem redesenhar no meio: um projeto grande tem centenas de linhas, e
	// deixar cada uma aparecer e sumir por conta propria faria a lista piscar a
	// cada tecla digitada.
	SendMessageW(g_scroll, WM_SETREDRAW, FALSE, 0);

	for (AsymRow& row : g_rows) {
		// Cabecalho fica, sempre. Sem isto, uma consulta que nao casasse com
		// "Position" esvaziava o dialogo e ele parecia quebrado.
		const bool visible = row.fixed || MatchesFilter(row.name, query);
		if (visible)
			++shown;

		// So mexe no que mudou. Entre uma tecla e a seguinte a maioria das
		// linhas continua no mesmo estado, e cada ShowWindow custa uma chamada
		// ao sistema por controle.
		if (visible == row.visible)
			continue;
		row.visible = visible;
		++touched;

		for (const AsymCell& cell : row.cells)
			ShowWindow(cell.window, visible ? SW_SHOW : SW_HIDE);
	}

	if (touched > 0) {
		// Um painel por vez: cada grupo recolhivel tem a propria regua de
		// lugares, e misturar as duas empilharia osso em cima de slider.
		std::vector<HWND> hosts;
		for (const AsymRow& row : g_rows) {
			bool known = false;
			for (HWND seen : hosts)
				known = known || (seen == row.host);
			if (!known)
				hosts.push_back(row.host);
		}
		for (HWND host : hosts)
			CompactHost(host);
	}

	ShrinkScrollRangeToContent();

	SendMessageW(g_scroll, WM_SETREDRAW, TRUE, 0);
	if (touched > 0)
		RedrawWindow(g_scroll, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);

	LogF("symmetrize: filtro '%ls' -- %d de %d linhas visiveis, %d mudaram",
		 query.c_str(), shown, static_cast<int>(g_rows.size()), touched);
}

// Reserva a faixa da busca acima da lista, do mesmo jeito que a busca do Batch
// Build faz: o wx posiciona primeiro, e so entao a lista encolhe.
int BandHeight(HWND dlg) {
	HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
	HDC dc = GetDC(dlg);
	if (!dc)
		return 26;

	HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
	TEXTMETRICW tm = {};
	GetTextMetricsW(dc, &tm);
	if (old)
		SelectObject(dc, old);
	ReleaseDC(dlg, dc);

	const int height = static_cast<int>(tm.tmHeight) + 12;
	return height < 22 ? 22 : height;
}

bool g_layouting = false;

void ApplyLayout() {
	if (!g_dialog || !g_edit || !g_scroll || !IsWindow(g_scroll))
		return;

	// Reposicionar a area que rola gera um WM_SIZE nela, que volta para ca. A
	// checagem de idempotencia abaixo ja cortaria a segunda volta, mas uma
	// trava explicita torna isso obvio para quem le.
	if (g_layouting)
		return;
	g_layouting = true;

	RECT sr = {};
	GetWindowRect(g_scroll, &sr);
	MapWindowPoints(nullptr, g_dialog, reinterpret_cast<POINT*>(&sr), 2);

	// Idempotencia: sem ela, cada WM_SIZE encolheria a lista mais uma faixa, e
	// WM_SHOWWINDOW, WM_SIZE e a mensagem adiada chegam em sequencia.
	if (EqualRect(&sr, &g_appliedScrollRect)) {
		g_layouting = false;
		return;
	}

	const int band = BandHeight(g_dialog);
	const int width = sr.right - sr.left;
	const int height = sr.bottom - sr.top;
	if (width <= 20 || height <= band * 2) {
		g_layouting = false;
		return;
	}

	SetWindowPos(g_scroll, nullptr, sr.left, sr.top + band, width, height - band,
				 SWP_NOZORDER | SWP_NOACTIVATE);

	// A caixa vai para o TOPO da z-order, e nao fica onde nasceu.
	//
	// Expandir um dos paineis recolhiveis faz o wx refazer o layout e
	// repintar a moldura do grupo por cima dela -- foi assim que a busca
	// simplesmente sumiu da tela depois de expandir "Bones".
	//
	// A largura tambem nao e a da lista inteira: ocupar tudo cobria o titulo
	// "Vertex Data Asymmetries".
	const int searchWidth = (width > 420) ? 360 : (width - 40);
	SetWindowPos(g_edit, HWND_TOP, sr.right - searchWidth, sr.top + 1, searchWidth, band - 5,
				 SWP_NOACTIVATE);

	SetRect(&g_appliedScrollRect, sr.left, sr.top + band, sr.right, sr.bottom);
	g_layouting = false;
}

// A area que rola tambem e observada, e nao so o dialogo.
//
// Expandir "Sliders" ou "Bones" refaz o layout DELA sem tocar no tamanho do
// dialogo, entao o WM_SIZE do dialogo nunca chega e a faixa da busca nao era
// reaplicada -- a lista voltava ao tamanho cheio e engolia a caixa.
LRESULT CALLBACK ScrollSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, ScrollSubclassProc, id);

	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
	if (msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED)
		ApplyLayout();
	return result;
}

LRESULT CALLBACK DialogSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (g_deferredLayout && msg == g_deferredLayout) {
		ApplyLayout();
		return 0;
	}

	switch (msg) {
		case WM_COMMAND:
			// Compara o handle e nao so o id: se algum controle do dialogo
			// tiver o mesmo id, comparar por id sozinho misturaria os dois.
			if (HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == g_edit) {
				ApplyFilter();
				return 0;
			}
			break;

		// So a NOSSA caixa. Os controles do dialogo sao do wx, que ja cuida do
		// tema deles.
		case WM_CTLCOLOREDIT:
			if (g_dark && reinterpret_cast<HWND>(lParam) == g_edit) {
				SetTextColor(reinterpret_cast<HDC>(wParam), kDarkText);
				SetBkColor(reinterpret_cast<HDC>(wParam), kDarkControlBackground);
				return reinterpret_cast<LRESULT>(EditBackgroundBrush());
			}
			break;

		case WM_SHOWWINDOW:
		case WM_SIZE: {
			LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
			ApplyLayout();
			return r;
		}

		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, DialogSubclassProc, id);
			g_dialog = nullptr;
			g_scroll = nullptr;
			g_edit = nullptr;
			g_rows.clear();
			SetRectEmpty(&g_appliedScrollRect);
			break;

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Enter na caixa de busca dispararia o botao padrao do dialogo, que aqui e
// "Mask"/"Symmetrize" -- ou seja, executaria a operacao. Aqui ele nao faz nada:
// o filtro ja e aplicado a cada tecla.
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	if (msg == WM_GETDLGCODE) {
		LRESULT code = DefSubclassProc(hwnd, msg, wParam, lParam);
		auto* incoming = reinterpret_cast<MSG*>(lParam);
		if (incoming && incoming->message == WM_KEYDOWN && incoming->wParam == VK_RETURN)
			code |= DLGC_WANTMESSAGE;
		return code;
	}

	if (msg == WM_KEYDOWN && wParam == VK_RETURN)
		return 0; // engolido: Esc e Tab seguem o caminho normal do dialogo

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void AddSearchBox(HWND dlg) {
	HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
	HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dlg, GWLP_HINSTANCE));

	HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
								WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
								0, 0, 10, 10, dlg,
								reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdSearch)), inst, nullptr);
	if (!edit) {
		LogF("symmetrize: nao consegui criar a caixa de busca (erro %lu)", GetLastError());
		return;
	}

	if (font)
		SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search sliders and bones..."));
	SetWindowSubclass(edit, EditSubclassProc, kEditSubclassId, 0);

	g_dark = DetectAppearance(AppDir()) == Appearance::Dark;
	if (g_dark)
		ApplyDarkControlTheme(edit, true);

	g_edit = edit;
}

void HandleAsymDialog(HWND dlg, HWND scroll) {
	g_dialog = dlg;
	g_scroll = scroll;
	g_edit = nullptr;
	g_rows.clear();
	SetRectEmpty(&g_appliedScrollRect);

	// As linhas moram em mais de um painel: as fixas ficam direto na area que
	// rola, e as de slider e de osso, cada uma dentro do seu painel recolhivel.
	std::vector<RowHost> hosts;
	CollectRowHosts(scroll, hosts, 0);

	// O painel mais raso e o do cabecalho. Tudo mais fundo e conteudo, e e so
	// isso que o filtro esconde.
	int shallowest = 99;
	for (const RowHost& host : hosts)
		shallowest = (host.depth < shallowest) ? host.depth : shallowest;

	int fixedRows = 0;
	for (const RowHost& host : hosts) {
		const bool isHeader = (host.depth == shallowest);
		for (const AsymRow& row : GroupRowsByTop(host.window)) {
			AsymRow copy = row;
			copy.fixed = isHeader;
			if (isHeader)
				++fixedRows;
			g_rows.push_back(copy);
		}
	}

	if (g_rows.empty()) {
		LogF("symmetrize: nenhuma linha encontrada, busca nao instalada");
		g_dialog = nullptr;
		g_scroll = nullptr;
		return;
	}

	AddSearchBox(dlg);
	SetWindowSubclass(dlg, DialogSubclassProc, kDialogSubclassId, 0);
	SetWindowSubclass(scroll, ScrollSubclassProc, kScrollSubclassId, 0);

	// O layout so vale depois que o dialogo estiver montado: aqui ainda
	// estamos dentro do WM_WINDOWPOSCHANGING que o exibe, e o retangulo da
	// lista e o provisorio.
	if (!g_deferredLayout)
		g_deferredLayout = RegisterWindowMessageW(L"BSOSImprovements_SymmetrizeLayout");
	if (g_deferredLayout)
		PostMessageW(dlg, g_deferredLayout, 0, 0);

	LogF("symmetrize: busca instalada, %d linhas em %d paineis (%d de cabecalho, que nunca somem)",
		 static_cast<int>(g_rows.size()), static_cast<int>(hosts.size()), fixedRows);

	// A estrutura deste dialogo so existe enquanto ele esta aberto, e ele e
	// modal. Despejar aqui e o unico jeito de olhar para ela sem depender de o
	// usuario lembrar de apertar uma tecla no momento certo.
	if (Cfg().dumpWindows)
		Diag::DumpWindowTree(dlg, "dialogo de simetria");
}

LRESULT CALLBACK PendingDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_WINDOWPOSCHANGING) {
		auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
		if (pos && (pos->flags & SWP_SHOWWINDOW)) {
			RemoveWindowSubclass(hwnd, PendingDialogProc, id);

			// Segunda tranca. O id do comando ja diz qual dialogo e este, mas
			// se algum dia ele abrir outra janela antes, a forma tem que bater
			// tambem: uma area com pelo menos duas caixas de marcacao dentro.
			HWND scroll = FindAsymScroll(hwnd);
			if (scroll)
				HandleAsymDialog(hwnd, scroll);
			else
				LogF("symmetrize: o dialogo aberto nao tem a forma esperada, ignorado");
		}
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code != HCBT_CREATEWND || !g_expecting)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	HWND candidate = reinterpret_cast<HWND>(wParam);
	auto* created = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
	if (!candidate || !created || !created->lpcs)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	if (created->lpcs->hwndParent == g_frame && ClassOf(candidate) == L"#32770")
		SetWindowSubclass(candidate, PendingDialogProc, kPendingSubclassId, 0);

	return CallNextHookEx(g_hook, code, wParam, lParam);
}

// Reconhece o dialogo pelo comando que o abriu, e nao pela forma dele.
//
// A forma nao basta: o Outfit Studio tem uma duzia de dialogos, e mais de um
// deles e uma lista com caixas de marcacao e dois botoes. Ja o comando e
// exato -- e enquanto ele esta sendo tratado, o dialogo que nascer e ele. O
// tratamento e sincrono porque o dialogo e modal, entao a janela e criada
// dentro desta chamada.
LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, FrameSubclassProc, id);

	if (msg != WM_COMMAND)
		return DefSubclassProc(hwnd, msg, wParam, lParam);

	const UINT command = LOWORD(wParam);
	if (command == 0 || (command != g_maskSymVertId && command != g_symVertId))
		return DefSubclassProc(hwnd, msg, wParam, lParam);

	g_expecting = true;
	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
	g_expecting = false;
	return result;
}

void InstallHere(void*) {
	if (!SetWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId, 0))
		LogF("symmetrize: nao consegui subclassar o frame");

	g_hook = SetWindowsHookExW(WH_CBT, CbtProc, SelfModule(), GetCurrentThreadId());
	if (!g_hook)
		LogF("symmetrize: SetWindowsHookEx(WH_CBT) falhou (erro %lu)", GetLastError());
}

void UninstallHere(void*) {
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId);
}

bool Enabled(const Config& cfg) {
	return cfg.symmetrizeSearch;
}

} // namespace

std::vector<AsymRow> GroupRowsByTop(HWND parent) {
	std::vector<AsymRow> rows;
	if (!parent)
		return rows;

	// Geometria e texto de cada filho, uma vez so.
	//
	// O agrupamento compara cada caixa com todos os vizinhos, e num projeto
	// grande sao centenas de linhas. Perguntar o retangulo e o texto ao sistema
	// dentro do laco de dentro custaria milhoes de chamadas na abertura do
	// dialogo, com a janela parada esperando.
	struct Child {
		HWND window = nullptr;
		RECT rect = {};
		std::wstring text;
		bool check = false;
	};

	std::vector<Child> children;
	for (HWND window : ChildrenOf(parent)) {
		Child entry;
		entry.window = window;
		entry.rect = RectInParent(window);
		entry.check = IsCheckBox(window);
		entry.text = TextOf(window);
		children.push_back(entry);
	}

	for (const Child& anchor : children) {
		if (!anchor.check)
			continue;

		AsymRow row;
		row.check = anchor.window;
		row.host = parent;
		row.top = static_cast<int>(anchor.rect.top);
		row.cells.push_back({anchor.window, 0});
		row.name = anchor.text;

		for (const Child& sibling : children) {
			if (sibling.window == anchor.window)
				continue;

			// Outra caixa de marcacao na mesma altura e outra linha, nao uma
			// celula desta.
			if (sibling.check)
				continue;

			// Mesma linha = o centro vertical do vizinho cai dentro da faixa da
			// caixa de marcacao. Comparar o topo cru falharia: os controles de
			// uma linha tem alturas diferentes e ficam centralizados entre si.
			const LONG center = sibling.rect.top + (sibling.rect.bottom - sibling.rect.top) / 2;
			if (center < anchor.rect.top || center > anchor.rect.bottom)
				continue;

			row.cells.push_back(
				{sibling.window, static_cast<int>(sibling.rect.top - anchor.rect.top)});

			if (!sibling.text.empty()) {
				if (!row.name.empty())
					row.name.push_back(L' ');
				row.name += sibling.text;
			}
		}

		rows.push_back(row);
	}

	std::sort(rows.begin(), rows.end(),
			  [](const AsymRow& a, const AsymRow& b) { return a.top < b.top; });
	return rows;
}

std::vector<int> CompactRowTops(const std::vector<int>& tops, const std::vector<bool>& visible) {
	std::vector<int> out;
	if (tops.size() != visible.size())
		return out;

	out.reserve(tops.size());
	size_t slot = 0;
	for (size_t i = 0; i < tops.size(); ++i) {
		if (!visible[i]) {
			// Escondida fica onde estava. Move-la seria trabalho invisivel, e
			// o topo dela e o que permite reconstruir a regua depois.
			out.push_back(tops[i]);
			continue;
		}
		out.push_back(tops[slot]);
		++slot;
	}
	return out;
}

HWND FindAsymScroll(HWND dlg) {
	if (!dlg)
		return nullptr;

	// A janela que ROLA, e nao simplesmente um filho do dialogo.
	//
	// A versao anterior pegava o filho direto com mais caixas de marcacao, e
	// isso podia cair na moldura do grupo em vez da area que rola de verdade
	// -- e ai a faixa da busca era reservada na janela errada, cortando o
	// topo da lista.
	HWND best = nullptr;
	int bestCount = 0;

	for (HWND candidate : AllDescendants(dlg)) {
		if ((GetWindowLongW(candidate, GWL_STYLE) & WS_VSCROLL) == 0)
			continue;
		const int count = CountCheckBoxesUnder(candidate, 0);
		if (count > bestCount) {
			bestCount = count;
			best = candidate;
		}
	}

	// Sem nenhuma que role, vale o antigo: melhor a moldura do que nada.
	if (bestCount < 2) {
		for (HWND child : ChildrenOf(dlg)) {
			const int count = CountCheckBoxesUnder(child, 0);
			if (count > bestCount) {
				bestCount = count;
				best = child;
			}
		}
	}

	return bestCount >= 2 ? best : nullptr;
}

namespace SymmetrizeSearch {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	HMENU bar = GetMenu(frame);
	if (!bar) {
		LogF("symmetrize: o frame nao tem menubar");
		return false;
	}

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	const MenuTrail maskTrail = ResolveMenuTrail(xrc.c_str(), "maskSymVert");
	const MenuTrail symTrail = ResolveMenuTrail(xrc.c_str(), "symVert");

	g_maskSymVertId = maskTrail.empty()
						  ? 0
						  : CommandIdAtLabeledPath(bar, maskTrail.path, maskTrail.labels);
	g_symVertId = symTrail.empty()
					  ? 0
					  : CommandIdAtLabeledPath(bar, symTrail.path, symTrail.labels);

	if (g_maskSymVertId == 0 && g_symVertId == 0) {
		LogF("symmetrize: nao resolvi nenhum dos dois comandos, feature desligada");
		return false;
	}

	LogF("symmetrize: comandos maskSymVert=%u symVert=%u", g_maskSymVertId, g_symVertId);

	if (!RunOnUiThread(frame, InstallHere, nullptr))
		return false;
	return g_hook != nullptr;
}

void Uninstall() {
	if (g_hook) {
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
	}
	if (g_frame)
		RunOnUiThread(g_frame, UninstallHere, nullptr);

	g_frame = nullptr;
	g_dialog = nullptr;
	g_scroll = nullptr;
	g_edit = nullptr;
	g_expecting = false;
	g_maskSymVertId = 0;
	g_symVertId = 0;
	g_rows.clear();
}

} // namespace SymmetrizeSearch

BSOS_REGISTER_FEATURE(symmetrize, "busca no symmetrize", HostApp::OutfitStudio, Enabled,
					  SymmetrizeSearch::Install, SymmetrizeSearch::Uninstall)
