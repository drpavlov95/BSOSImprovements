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
const int kIdResults = 0xBF04;
const UINT_PTR kFrameSubclassId = 0xB50F;
const UINT_PTR kPendingSubclassId = 0xB510;
const UINT_PTR kDialogSubclassId = 0xB511;
const UINT_PTR kEditSubclassId = 0xB512;
const UINT_PTR kScrollSubclassId = 0xB513;

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
HWND g_dialog = nullptr;

// A area que rola do dialogo -- o "asymScroll" do Actions.xrc.
//
// Ela para de ser DESENHADA assim que as linhas sao lidas, e nao volta enquanto
// o dialogo existir. Continua sendo o motor: as caixas de marcacao de verdade
// moram nela, e e nelas que o clique do usuario acaba caindo. Mas quem o usuario
// ve e a nossa lista, no lugar dela.
//
// No lugar dela, e nao por cima: duas listas empilhadas se pintam as duas, a de
// baixo reaparece por um quadro num redesenho, e o cabecalho "Type / Average /
// Count" dela fica atras do cabecalho da nossa -- dois cabecalhos iguais, um
// invisivel.
HWND g_scroll = nullptr;

HWND g_edit = nullptr;

// A lista do dialogo. A unica que se ve.
//
// Ocupa o retangulo que era da area que rola, e essa nao se desenha mais. Com
// busca vazia mostra tudo; digitar so tira o que nao casa.
HWND g_results = nullptr;

// Verdadeiro enquanto SOMOS nos mexendo na lista de resultados.
//
// Preencher a lista dispara a mesma notificacao de um clique do usuario, e sem
// esta trava marcar as caixas na montagem sairia clicando nas caixas de verdade
// do programa -- ou seja, a busca mudaria o que vai ser simetrizado.
bool g_syncing = false;

// As tres colunas de uma linha, ja separadas.
struct RowText {
	std::wstring name;
	std::wstring average;
	std::wstring count;
};

// Para cada item da lista, de qual linha ele veio.
std::vector<int> g_shown;

// O texto de cada linha, ja separado em colunas, na mesma ordem de g_rows.
//
// Calculado uma vez, na abertura. Separar as colunas custa quatro leituras de
// texto e quatro de retangulo por linha, e refazer isso a cada tecla digitada
// seriam mil e cem chamadas ao sistema por letra, com cento e quarenta linhas.
std::vector<RowText> g_text;

bool g_dark = false;
UINT g_deferredLayout = 0;
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
// A profundidade e o que separa cabecalho de conteudo: as linhas de "Position"
// e das contagens moram direto na area que rola, num wxFlexGridSizer, e as de
// slider e de osso ficam mais fundo, cada grupo dentro do seu wxCollapsiblePane.
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

// Uma janela da arvore, e a que distancia da raiz ela esta.
//
// A profundidade importa porque duas janelas encaixadas uma na outra contam as
// MESMAS caixas de marcacao, e so ela desempata.
struct Descendant {
	HWND window = nullptr;
	int depth = 0;
};

void CollectAllDescendants(HWND root, std::vector<Descendant>& out, int depth) {
	if (depth > 8)
		return;
	for (HWND child : ChildrenOf(root)) {
		out.push_back({child, depth});
		CollectAllDescendants(child, out, depth + 1);
	}
}

std::vector<Descendant> AllDescendants(HWND root) {
	std::vector<Descendant> out;
	CollectAllDescendants(root, out, 0);
	return out;
}

// As tres colunas de uma linha, separadas pela posicao na tela.
//
// Por posicao e nao por classe de controle: nas linhas de slider e de osso o
// nome vem na propria caixa de marcacao, e nas de cabecalho vem num texto ao
// lado dela. Ordenar pela coordenada acerta os dois casos sem precisar saber
// qual e qual.
RowText SplitRow(const AsymRow& row) {
	struct Piece {
		int left = 0;
		std::wstring text;
	};

	std::vector<Piece> pieces;
	for (HWND cell : row.cells) {
		std::wstring text = TextOf(cell);
		if (text.empty())
			continue;
		pieces.push_back({static_cast<int>(RectInParent(cell).left), std::move(text)});
	}
	std::sort(pieces.begin(), pieces.end(),
			  [](const Piece& a, const Piece& b) { return a.left < b.left; });

	// O nome vem da ESQUERDA, a contagem da DIREITA, e a media e o que sobra no
	// meio.
	//
	// Contar da esquerda para a direita punha a contagem na coluna errada em tres
	// linhas. O Actions.xrc explica: a linha do "x Sliders" e
	//
	//   checkAnySlider | anySliderLabel | spacer | anySliderText
	//
	// e o spacer nao tem texto. Sobravam duas pecas, a segunda virava "media", e
	// o 7176 aparecia debaixo de Average em vez de Count -- justo nas linhas que
	// ficam sempre no topo, onde mais se repara.
	//
	// Pela direita nao ha esse problema: a contagem e sempre a ultima coluna, em
	// toda linha, com ou sem media.
	RowText out;
	if (pieces.empty())
		return out;

	out.name = pieces.front().text;
	if (pieces.size() > 1)
		out.count = pieces.back().text;
	if (pieces.size() > 2)
		out.average = pieces[pieces.size() - 2].text;
	return out;
}

// Reserva a faixa da busca acima da lista.
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

// Poe a busca e a lista dentro da area que era da lista original.
//
// DENTRO, e sem mover uma unica janela do programa.
//
// A versao anterior empurrava a moldura "Vertex Data Asymmetries" inteira para
// baixo para abrir uma faixa onde por a caixa de busca. O log mediu o
// deslocamento: a lista saia de 95..495 e ia para 123..523 -- vinte e oito
// pixels de layout do proprio dialogo andando, com um EDIT cru flutuando na
// folga aberta. Era esse o aspecto de remendo, e ele era meu.
//
// O retangulo da lista original e nosso para usar: ela nao se desenha mais, e
// ninguem mais le aquela area. A busca sai de dentro dele, com a lista logo
// abaixo. O dialogo fica com a geometria que o wx lhe deu, exatamente, para
// sempre.
void LayoutOurControls() {
	if (!g_dialog || !g_edit || !g_results || !g_scroll || !IsWindow(g_scroll))
		return;

	RECT rc = {};
	GetWindowRect(g_scroll, &rc);
	MapWindowPoints(nullptr, g_dialog, reinterpret_cast<POINT*>(&rc), 2);

	const int left = static_cast<int>(rc.left);
	const int top = static_cast<int>(rc.top);
	const int width = static_cast<int>(rc.right - rc.left);
	const int height = static_cast<int>(rc.bottom - rc.top);

	// Antes de o dialogo estar montado o retangulo ainda e provisorio, e
	// posicionar por ele poria a busca fora do lugar.
	if (width < 80 || height < 80)
		return;

	const int band = BandHeight(g_dialog);
	SetWindowPos(g_edit, HWND_TOP, left, top, width, band - 4,
				 SWP_NOACTIVATE | SWP_SHOWWINDOW);
	SetWindowPos(g_results, HWND_TOP, left, top + band, width, height - band,
				 SWP_NOACTIVATE | SWP_SHOWWINDOW);

	// Os numeros tem largura fixa e o nome fica com o resto, como na lista
	// original: la a coluna do meio e a growablecol do wxFlexGridSizer.
	const int average = 100;
	const int count = 80;
	const int name = width - average - count - GetSystemMetrics(SM_CXVSCROLL) - 8;
	ListView_SetColumnWidth(g_results, 0, name > 120 ? name : 120);
	ListView_SetColumnWidth(g_results, 1, average);
	ListView_SetColumnWidth(g_results, 2, count);

	// Uma vez por retangulo novo.
	//
	// Duas coisas para conferir sem precisar de uma captura de tela. Se o topo
	// mudar sozinho entre duas linhas, alguem ainda esta empurrando o layout do
	// dialogo -- foi assim que os vinte e oito pixels apareceram. E se o rodape da
	// lista chegar perto da altura do dialogo, ela esta comendo o "Vertices that
	// will be symmetrized:" e os botoes.
	static RECT logged = {};
	if (!EqualRect(&rc, &logged)) {
		logged = rc;
		RECT client = {};
		GetClientRect(g_dialog, &client);
		LogF("symmetrize: busca em %d..%d, lista em %d..%d, e o dialogo tem %ld de altura",
			 top, top + band - 4, top + band, top + height,
			 client.bottom - client.top);
	}
}

// Refaz a lista a partir do que esta na caixa de busca.
//
// Busca vazia mostra TUDO, e nao esconde a lista: ela e a lista do dialogo, nao
// um resultado de busca que vai e volta. Digitar so tira o que nao casa.
void RefreshResults() {
	if (!g_results || !IsWindow(g_results))
		return;

	const std::wstring query = SearchText();

	g_syncing = true;
	SendMessageW(g_results, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(g_results);
	g_shown.clear();

	int item = 0;
	for (size_t i = 0; i < g_rows.size() && i < g_text.size(); ++i) {
		const AsymRow& row = g_rows[i];
		const RowText& text = g_text[i];

		// As linhas de cabecalho -- "Position", "111 sliders", "26 bones" --
		// ficam sempre no topo. Elas marcam varias de uma vez, e some-las
		// durante a busca tiraria do usuario justamente o atalho que ele usa
		// depois de achar o grupo que queria.
		//
		// A busca casa com o NOME, e nao com a linha inteira. AsymRow::name
		// junta tudo o que esta na altura da caixa -- "PecsClavicle 0.000001
		// 132" -- entao digitar "1" casava com quase todas por causa dos
		// numeros, e digitar "0" casava com todas.
		if (!row.fixed && !MatchesFilter(text.name, query))
			continue;

		LVITEMW entry = {};
		entry.mask = LVIF_TEXT;
		entry.iItem = item;
		entry.pszText = const_cast<wchar_t*>(text.name.c_str());
		if (ListView_InsertItem(g_results, &entry) < 0)
			continue;

		ListView_SetItemText(g_results, item, 1, const_cast<wchar_t*>(text.average.c_str()));
		ListView_SetItemText(g_results, item, 2, const_cast<wchar_t*>(text.count.c_str()));

		// O resultado nasce com a marcacao que a caixa de verdade tem. Sem isto
		// a busca pareceria desmarcar tudo o que o usuario ja tinha escolhido.
		ListView_SetCheckState(g_results, item,
							   SendMessageW(row.check, BM_GETCHECK, 0, 0) == BST_CHECKED);

		g_shown.push_back(static_cast<int>(i));
		++item;
	}

	SendMessageW(g_results, WM_SETREDRAW, TRUE, 0);
	g_syncing = false;

	LayoutOurControls();
	RedrawWindow(g_results, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);

	LogF("symmetrize: busca '%ls' -- %d de %d linhas na lista", query.c_str(), item,
		 static_cast<int>(g_rows.size()));
}

// O usuario marcou ou desmarcou um resultado.
void OnResultToggled(const NMLISTVIEW* info) {
	// A notificacao vale para qualquer mudanca de estado do item -- selecao,
	// foco, marcacao. So a marcacao interessa, e ela mora nos quatro bits altos
	// do estado; zero ali quer dizer "nao foi a marcacao".
	const UINT before = (info->uOldState & LVIS_STATEIMAGEMASK) >> 12;
	const UINT after = (info->uNewState & LVIS_STATEIMAGEMASK) >> 12;
	if (before == 0 || after == 0 || before == after)
		return;

	const int item = info->iItem;
	if (item < 0 || item >= static_cast<int>(g_shown.size()))
		return;

	const int index = g_shown[static_cast<size_t>(item)];
	if (index < 0 || index >= static_cast<int>(g_rows.size()))
		return;

	HWND check = g_rows[static_cast<size_t>(index)].check;
	if (!check || !IsWindow(check))
		return;

	const bool wanted = (after == 2);
	const bool current = SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
	if (wanted == current)
		return;

	// BM_CLICK, e nao BM_SETCHECK.
	//
	// O programa so recalcula "Vertices that will be symmetrized" quando RECEBE
	// o clique -- a conta esta no tratador dele, nao no controle. BM_SETCHECK
	// mudaria o desenho da caixa e deixaria a conta parada, e o botao Symmetrize
	// agiria sobre outra coisa do que a tela mostra.
	SendMessageW(check, BM_CLICK, 0, 0);

	// A caixa esta ESCONDIDA junto com a lista original, e o clique precisa
	// valer mesmo assim. Ler o estado de volta prova que valeu: se ele virou, o
	// controle processou o clique, e quem processa manda o aviso ao dono -- que e
	// onde o programa refaz a conta. Se um dia isto parar de virar, e aqui que
	// aparece, e nao tres telas adiante.
	const bool landed = SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
	if (landed != wanted) {
		LogF("symmetrize: a caixa de '%ls' nao aceitou o clique -- continua %s",
			 g_rows[static_cast<size_t>(index)].name.c_str(), landed ? "marcada" : "desmarcada");
	}
}

// A area que rola tambem e observada, e nao so o dialogo.
//
// Ela esta escondida, mas continua sendo o wx quem lhe da tamanho: redimensionar
// o dialogo faz o sizer refazer o layout dela, e e do retangulo dela que a busca
// e a lista tiram o proprio. Sem observa-la, as duas ficariam onde estavam.
LRESULT CALLBACK ScrollSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, ScrollSubclassProc, id);

	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
	if (msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED)
		LayoutOurControls();
	return result;
}

LRESULT CALLBACK DialogSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (g_deferredLayout && msg == g_deferredLayout) {
		LayoutOurControls();
		return 0;
	}

	switch (msg) {
		case WM_COMMAND:
			// Compara o handle e nao so o id: se algum controle do dialogo
			// tiver o mesmo id, comparar por id sozinho misturaria os dois.
			if (HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == g_edit) {
				RefreshResults();
				return 0;
			}
			break;

		case WM_NOTIFY: {
			auto* head = reinterpret_cast<NMHDR*>(lParam);
			if (head && head->hwndFrom == g_results && head->code == LVN_ITEMCHANGED) {
				auto* info = reinterpret_cast<NMLISTVIEW*>(lParam);
				if (!g_syncing && info && (info->uChanged & LVIF_STATE))
					OnResultToggled(info);
				return 0;
			}
			break;
		}

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
			LayoutOurControls();
			return r;
		}

		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, DialogSubclassProc, id);
			g_dialog = nullptr;
			g_scroll = nullptr;
			g_edit = nullptr;
			g_results = nullptr;
			g_syncing = false;
			g_shown.clear();
			g_text.clear();
			g_rows.clear();
			break;

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Enter na caixa de busca dispararia o botao padrao do dialogo, que aqui e
// "Mask"/"Symmetrize" -- ou seja, executaria a operacao. Aqui ele nao faz nada:
// a lista de resultados ja e refeita a cada tecla.
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

	// Nascem invisiveis, as duas.
	//
	// O retangulo so vale depois que o dialogo estiver montado, e ate la elas
	// ficariam num canto com dez pixels de lado. Quem as mostra e o layout, ja no
	// lugar certo -- entao nao ha um quadro com a busca no canto superior
	// esquerdo da janela.
	HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
								WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
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

void AddResultsList(HWND dlg) {
	INITCOMMONCONTROLSEX common = {};
	common.dwSize = sizeof(common);
	common.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&common);

	HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
	HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dlg, GWLP_HINSTANCE));

	// A mesma borda da janela que ela substitui.
	//
	// Perguntada, e nao escolhida: se a area que rola tem moldura rebaixada, a
	// nossa tem que ter tambem, senao o lugar onde havia um recorte no dialogo
	// passa a ser uma mancha chapada. E se ela nao tem, por uma seria inventar
	// uma borda que o dialogo nao tinha.
	const DWORD edge = GetWindowLongW(g_scroll, GWL_EXSTYLE) & WS_EX_CLIENTEDGE;
	const DWORD border = GetWindowLongW(g_scroll, GWL_STYLE) & WS_BORDER;

	HWND list = CreateWindowExW(edge, WC_LISTVIEWW, L"",
								WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
									LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | border,
								0, 0, 10, 10, dlg,
								reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdResults)), inst,
								nullptr);
	if (!list) {
		LogF("symmetrize: nao consegui criar a lista de resultados (erro %lu)", GetLastError());
		return;
	}

	ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
												LVS_EX_DOUBLEBUFFER);
	if (font)
		SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

	// As mesmas tres colunas do cabecalho do dialogo, para a lista de resultados
	// se parecer com a lista que ela cobre.
	const wchar_t* titles[] = {L"Type", L"Average", L"Count"};
	const int formats[] = {LVCFMT_LEFT, LVCFMT_RIGHT, LVCFMT_RIGHT};
	for (int i = 0; i < 3; ++i) {
		LVCOLUMNW column = {};
		column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
		column.fmt = formats[i];
		column.cx = 100;
		column.pszText = const_cast<wchar_t*>(titles[i]);
		ListView_InsertColumn(list, i, &column);
	}

	if (g_dark) {
		ApplyDarkControlTheme(list, false);
		ListView_SetBkColor(list, kDarkControlBackground);
		ListView_SetTextBkColor(list, kDarkControlBackground);
		ListView_SetTextColor(list, kDarkText);
	}

	g_results = list;
}

void HandleAsymDialog(HWND dlg, HWND scroll) {
	g_dialog = dlg;
	g_scroll = scroll;
	g_edit = nullptr;
	g_results = nullptr;
	g_shown.clear();
	g_text.clear();
	g_rows.clear();

	// As linhas moram em mais de um painel: as de cabecalho ficam direto na area
	// que rola, num wxFlexGridSizer, e as de slider e de osso mais fundo, cada
	// grupo dentro do seu wxCollapsiblePane.
	std::vector<RowHost> hosts;
	CollectRowHosts(scroll, hosts, 0);

	// O painel mais raso e o do cabecalho. Tudo mais fundo e conteudo.
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
	AddResultsList(dlg);

	// As colunas de cada linha, separadas agora.
	//
	// Agora porque as posicoes ainda valem: e delas que sai qual texto e nome,
	// qual e media e qual e contagem.
	g_text.clear();
	g_text.reserve(g_rows.size());
	for (const AsymRow& row : g_rows)
		g_text.push_back(SplitRow(row));

	// A original sai de cena aqui, antes de o dialogo aparecer pela primeira vez.
	//
	// Por REGIAO VAZIA, e nao por ShowWindow.
	//
	// ShowWindow(SW_HIDE) e o obvio, e e uma armadilha. Nao da para saber, de
	// fora, se o wx trata o WM_SHOWWINDOW que ele provoca e anota que a janela
	// ficou escondida -- e se anotar, o sizer dele tira a area que rola do
	// layout na proxima passagem. Ele roda uma ao mostrar o dialogo e outra a
	// cada redimensionamento, e o resultado seria a moldura "Vertex Data
	// Asymmetries" desabando sobre si mesma, levando junto o retangulo de onde a
	// nossa lista tira o proprio.
	//
	// A regiao nao e uma propriedade que sizer nenhum consulte. Para o wx a
	// janela continua visivel e do mesmo tamanho -- GetWindowRect devolve o
	// retangulo inteiro, que e o que precisamos -- e para o usuario ela nao
	// existe: a regiao recorta o desenho dela e o dos filhos junto.
	//
	// O ponto forte nao e que eu saiba o que o wx faz. E que assim nao preciso
	// saber.
	if (HRGN empty = CreateRectRgn(0, 0, 0, 0)) {
		// A regiao passa a ser do sistema; apaga-la aqui a tiraria da janela.
		SetWindowRgn(scroll, empty, TRUE);
	}

	// Registrado com classe e nome: apagar a janela errada apagaria a moldura
	// inteira do grupo, e o log tem que dizer "asymScroll" -- se um dia disser
	// outra coisa, o defeito esta em FindAsymScroll e nao aqui.
	LogF("symmetrize: a area que rola %p [%ls] '%ls' saiu do desenho",
		 static_cast<void*>(scroll), ClassOf(scroll).c_str(), TextOf(scroll).c_str());

	// E a nossa ja nasce cheia, entao o dialogo nunca e visto sem lista.
	RefreshResults();
	SetWindowSubclass(dlg, DialogSubclassProc, kDialogSubclassId, 0);
	SetWindowSubclass(scroll, ScrollSubclassProc, kScrollSubclassId, 0);

	// O layout so vale depois que o dialogo estiver montado: aqui ainda
	// estamos dentro do WM_WINDOWPOSCHANGING que o exibe, e o retangulo da
	// lista e o provisorio.
	if (!g_deferredLayout)
		g_deferredLayout = RegisterWindowMessageW(L"BSOSImprovements_SymmetrizeLayout");
	if (g_deferredLayout)
		PostMessageW(dlg, g_deferredLayout, 0, 0);

	LogF("symmetrize: busca instalada, %d linhas em %d paineis (%d de cabecalho, que nao entram nos resultados)",
		 static_cast<int>(g_rows.size()), static_cast<int>(hosts.size()), fixedRows);
	LogF("symmetrize: area que rola %p (escondida), lista %p", static_cast<void*>(g_scroll),
		 static_cast<void*>(g_results));

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
		row.cells.push_back(anchor.window);
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

			row.cells.push_back(sibling.window);

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

HWND FindAsymScroll(HWND dlg) {
	if (!dlg)
		return nullptr;

	// A janela MAIS FUNDA que ainda carrega todas as caixas de marcacao.
	//
	// Mais funda, e nao a primeira que aparecer com muitas. O Actions.xrc poe a
	// lista dentro de um wxStaticBoxSizer -- o "Vertex Data Asymmetries" -- e a
	// moldura e a area que rola contam exatamente as mesmas linhas, porque uma
	// esta dentro da outra. Parar na moldura fazia tudo depois disso cair na
	// janela errada.
	//
	// A regra antiga preferia quem tivesse WS_VSCROLL, e isso parecia razoavel:
	// o XRC declara <style>wxVSCROLL</style> no asymScroll. So que o wx so poe a
	// barra quando ela e necessaria, e os dois grupos comecam recolhidos
	// (<collapsed>1</collapsed>), entao o conteudo cabe e o estilo nao esta la
	// na hora em que o dialogo abre.
	HWND best = nullptr;
	int bestCount = 0;
	int bestDepth = -1;

	for (const Descendant& candidate : AllDescendants(dlg)) {
		const int count = CountCheckBoxesUnder(candidate.window, 0);
		if (count < bestCount)
			continue;
		if (count == bestCount && candidate.depth <= bestDepth)
			continue;

		bestCount = count;
		bestDepth = candidate.depth;
		best = candidate.window;
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
	g_results = nullptr;
	g_syncing = false;
	g_expecting = false;
	g_maskSymVertId = 0;
	g_symVertId = 0;
	g_shown.clear();
	g_text.clear();
	g_rows.clear();
}

} // namespace SymmetrizeSearch

BSOS_REGISTER_FEATURE(symmetrize, "busca no symmetrize", HostApp::OutfitStudio, Enabled,
					  SymmetrizeSearch::Install, SymmetrizeSearch::Uninstall)
