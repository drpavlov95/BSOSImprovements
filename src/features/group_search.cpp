#include "features/group_search.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/registry.h"
#include "win32/winfind.h"

namespace {

// ---------------------------------------------------------------- logica pura

std::wstring TrimWide(const std::wstring& s) {
	size_t begin = s.find_first_not_of(L" \t\r\n");
	if (begin == std::wstring::npos)
		return std::wstring();
	size_t end = s.find_last_not_of(L" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

std::wstring LowerWide(std::wstring s) {
	for (wchar_t& c : s)
		c = static_cast<wchar_t>(std::towlower(c));
	return s;
}

// ------------------------------------------------------------- estado do modo

const int kIdSearch = kGroupSearchEdit;
const int kIdList = kGroupSearchList;
const int kIdCheckVisible = kGroupSearchCheckVisible;
const int kIdClearAll = kGroupSearchClearAll;
const int kIdCounter = kGroupSearchCounter;

const UINT_PTR kFrameSubclassId = 0xB506;

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
UINT g_showMsg = 0;
bool g_busy = false;      // reentrancia: o nosso proprio dialogo tambem ativa
bool g_refreshing = false; // repopular a lista dispara notificacao por item
bool g_dialogOpen = false; // impede dois dialogos sobre o mesmo estado global

std::vector<std::wstring> g_allGroups;  // todos os nomes, ordem original
std::vector<bool> g_checked;            // marcacao por indice ORIGINAL
std::vector<int> g_visible;             // indices originais visiveis agora

// ------------------------------------------------------- montagem do template

// Constroi um DLGTEMPLATE em memoria. Usar dialogo de verdade, em vez de uma
// janela comum, da navegacao por Tab, Esc para cancelar e Enter para OK de
// graca.
struct DialogTemplateBuilder {
	std::vector<BYTE> bytes;

	void Align(size_t boundary) {
		while (bytes.size() % boundary)
			bytes.push_back(0);
	}

	template <typename T>
	void Put(const T& value) {
		const BYTE* raw = reinterpret_cast<const BYTE*>(&value);
		bytes.insert(bytes.end(), raw, raw + sizeof(T));
	}

	void PutString(const wchar_t* text) {
		if (!text) {
			Put<WORD>(0);
			return;
		}
		for (const wchar_t* p = text; *p; ++p)
			Put<WORD>(static_cast<WORD>(*p));
		Put<WORD>(0);
	}

	void BeginDialog(DWORD style, short cx, short cy, WORD itemCount, const wchar_t* title) {
		Put<DWORD>(style);
		Put<DWORD>(0);          // dwExtendedStyle
		Put<WORD>(itemCount);
		Put<short>(0);          // x
		Put<short>(0);          // y
		Put<short>(cx);
		Put<short>(cy);
		Put<WORD>(0);           // sem menu
		Put<WORD>(0);           // classe padrao
		PutString(title);
		Put<WORD>(9);           // corpo da fonte
		PutString(L"Segoe UI");
	}

	void AddItem(DWORD style, short x, short y, short cx, short cy, WORD id,
				 WORD classAtom, const wchar_t* text) {
		Align(4);
		Put<DWORD>(style);
		Put<DWORD>(0); // dwExtendedStyle
		Put<short>(x);
		Put<short>(y);
		Put<short>(cx);
		Put<short>(cy);
		Put<WORD>(id);
		Put<WORD>(0xFFFF);
		Put<WORD>(classAtom);
		PutString(text);
		Put<WORD>(0); // sem dados de criacao
	}

	void AddCustomClass(DWORD style, short x, short y, short cx, short cy, WORD id,
						const wchar_t* className, const wchar_t* text) {
		Align(4);
		Put<DWORD>(style);
		Put<DWORD>(0);
		Put<short>(x);
		Put<short>(y);
		Put<short>(cx);
		Put<short>(cy);
		Put<WORD>(id);
		PutString(className);
		PutString(text);
		Put<WORD>(0);
	}
};

const WORD kAtomButton = 0x0080;
const WORD kAtomEdit = 0x0081;
const WORD kAtomStatic = 0x0082;

// ------------------------------------------------------------ nosso dialogo

// Escreve o contador. Um lugar so, para as duas origens -- repopular a lista e
// marcar um item -- nao divergirem no numero mostrado.
void UpdateCounter(HWND dlg, int visibleCount) {
	int checked = 0;
	for (bool on : g_checked)
		checked += on ? 1 : 0;

	wchar_t status[160];
	swprintf_s(status, L"%d of %d groups \x00b7 %d checked", visibleCount,
			   static_cast<int>(g_allGroups.size()), checked);
	SetDlgItemTextW(dlg, kIdCounter, status);
}

void RefreshList(HWND dlg) {
	HWND list = GetDlgItem(dlg, kIdList);
	HWND search = GetDlgItem(dlg, kIdSearch);

	wchar_t raw[256] = {};
	GetWindowTextW(search, raw, 256);
	const std::wstring query = TrimWide(raw);

	g_refreshing = true;
	SendMessageW(list, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(list);
	g_visible.clear();

	int row = 0;
	int checkedTotal = 0;
	for (size_t i = 0; i < g_allGroups.size(); ++i) {
		if (g_checked[i])
			++checkedTotal;
		if (!MatchesFilter(g_allGroups[i], query))
			continue;

		LVITEMW item = {};
		item.mask = LVIF_TEXT | LVIF_PARAM;
		item.iItem = row;
		item.pszText = const_cast<wchar_t*>(g_allGroups[i].c_str());
		item.lParam = static_cast<LPARAM>(i);

		// So conta a linha se ela entrou mesmo. g_visible mapeia linha visivel
		// para indice original, e o sincronismo de marcacao le a lista por
		// esse mapa -- se ele ficasse maior que a lista, o sincronismo leria
		// linhas inexistentes, que devolvem "desmarcado", e apagaria marcacoes
		// boas.
		if (ListView_InsertItem(list, &item) < 0)
			continue;

		ListView_SetCheckState(list, row, g_checked[i] ? TRUE : FALSE);
		g_visible.push_back(static_cast<int>(i));
		++row;
	}
	SendMessageW(list, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(list, nullptr, TRUE);

	g_refreshing = false;
	UpdateCounter(dlg, row);
	(void)checkedTotal;
}

// Le as marcacoes da list view de volta para o vetor por indice original. Isso
// e o que preserva a marcacao de itens escondidos pelo filtro: so as linhas
// visiveis sao tocadas.
void SyncChecksFromList(HWND dlg) {
	HWND list = GetDlgItem(dlg, kIdList);

	// Nunca ler alem do que a lista tem de fato: linha inexistente devolve
	// "desmarcado", e isso apagaria a marcacao do grupo correspondente.
	const size_t rows = static_cast<size_t>(ListView_GetItemCount(list));
	const size_t limit = rows < g_visible.size() ? rows : g_visible.size();

	for (size_t row = 0; row < limit; ++row)
		g_checked[g_visible[row]] = ListView_GetCheckState(list, static_cast<int>(row)) != FALSE;
}

// Centraliza na janela dona, como o dialogo original do wx faz, e mantem tudo
// dentro da area util do monitor onde essa janela esta.
void CenterOnOwner(HWND dlg) {
	HWND owner = g_frame && IsWindow(g_frame) ? g_frame : GetWindow(dlg, GW_OWNER);
	if (!owner)
		return;

	RECT ownerRect = {};
	RECT dlgRect = {};
	if (!GetWindowRect(owner, &ownerRect) || !GetWindowRect(dlg, &dlgRect))
		return;

	const int width = dlgRect.right - dlgRect.left;
	const int height = dlgRect.bottom - dlgRect.top;
	int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
	int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
		if (x + width > mi.rcWork.right)
			x = mi.rcWork.right - width;
		if (y + height > mi.rcWork.bottom)
			y = mi.rcWork.bottom - height;
		if (x < mi.rcWork.left)
			x = mi.rcWork.left;
		if (y < mi.rcWork.top)
			y = mi.rcWork.top;
	}

	SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_INITDIALOG: {
			HWND list = GetDlgItem(dlg, kIdList);
			ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

			RECT rc = {};
			GetClientRect(list, &rc);
			LVCOLUMNW col = {};
			col.mask = LVCF_WIDTH;
			col.cx = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL) - 4;
			ListView_InsertColumn(list, 0, &col);

			CenterOnOwner(dlg);
			RefreshList(dlg);
			SetFocus(GetDlgItem(dlg, kIdSearch));
			return FALSE; // ja definimos o foco
		}

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case kIdSearch:
					if (HIWORD(wParam) == EN_CHANGE) {
						SyncChecksFromList(dlg);
						RefreshList(dlg);
					}
					return TRUE;

				case kIdCheckVisible: {
					SyncChecksFromList(dlg);
					for (int index : g_visible)
						g_checked[index] = true;
					RefreshList(dlg);
					return TRUE;
				}

				case kIdClearAll:
					// Limpa tudo, inclusive o que esta escondido pelo filtro.
					std::fill(g_checked.begin(), g_checked.end(), false);
					RefreshList(dlg);
					return TRUE;

				case IDOK:
					SyncChecksFromList(dlg);
					EndDialog(dlg, IDOK);
					return TRUE;

				case IDCANCEL:
					EndDialog(dlg, IDCANCEL);
					return TRUE;

				default:
					break;
			}
			break;

		case WM_NOTIFY: {
			auto* header = reinterpret_cast<NMHDR*>(lParam);
			// g_refreshing: repovoar a lista dispara LVN_ITEMCHANGED a cada
			// item inserido. Sem esta guarda, o sincronismo abaixo leria o
			// estado de marcacao de uma lista pela metade e escreveria de
			// volta em g_checked -- lendo, portanto, o que ele mesmo ainda nao
			// terminou de montar.
			if (!g_refreshing && header->idFrom == kIdList && header->code == LVN_ITEMCHANGED) {
				auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
				// Bit de imagem de estado mudou: alguem marcou ou desmarcou.
				if (changed->uChanged & LVIF_STATE) {
					SyncChecksFromList(dlg);
					UpdateCounter(dlg, static_cast<int>(g_visible.size()));
				}
			}
			break;
		}

		default:
			break;
	}
	return FALSE;
}

INT_PTR ShowOurDialog(HWND owner) {
	std::vector<BYTE> tpl = BuildGroupsDialogTemplate();
	return DialogBoxIndirectParamW(SelfModule(),
								   reinterpret_cast<LPCDLGTEMPLATEW>(tpl.data()),
								   owner, DialogProc, 0);
}

// ------------------------------------------------- deteccao do dialogo do wx

// Estrutura do wxMultiChoiceDialog: um dialogo #32770 com exatamente um
// ListBox e pelo menos dois Buttons. Nunca casar por titulo: o BodySlide traz
// traducoes para mais de 30 idiomas. E o unico wxMultiChoiceDialog do processo
// do BodySlide, entao a estrutura basta.
std::wstring ListBoxItemText(HWND listBox, int index);

HWND ChooseGroupsListBoxImpl(HWND dlg) {
	HWND listBox = nullptr;
	int listBoxes = 0;
	int buttons = 0;
	int edits = 0;
	int combos = 0;

	for (HWND child : ChildrenOf(dlg)) {
		std::wstring cls = ClassOf(child);
		if (_wcsicmp(cls.c_str(), L"ListBox") == 0) {
			++listBoxes;
			listBox = child;
		} else if (_wcsicmp(cls.c_str(), L"Button") == 0) {
			++buttons;
		} else if (_wcsicmp(cls.c_str(), L"Edit") == 0 || _wcsnicmp(cls.c_str(), L"RichEdit", 8) == 0) {
			++edits;
		} else if (_wcsicmp(cls.c_str(), L"ComboBox") == 0) {
			++combos;
		}
	}

	// Pre-filtro barato: uma unica lista de marcacao (wxCheckListBox e
	// owner-drawn no MSW) e ao menos dois botoes.
	if (listBoxes != 1 || buttons < 2 || !listBox)
		return nullptr;
	(void)edits;
	(void)combos;

	// Exatamente um listbox e dois botoes tambem descreve varios outros
	// dialogos. O que distingue um wxCheckListBox e ele ser owner-drawn: o wx
	// desenha a caixinha de marcacao por conta propria. Um listbox comum nao
	// tem esse estilo.
	const LONG style = GetWindowLongW(listBox, GWL_STYLE);
	if ((style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) == 0)
		return nullptr;

	const int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
	if (count <= 0)
		return nullptr;

	// O criterio decisivo e o CONTEUDO, nao a estrutura.
	//
	// Estrutura nao separa: dlgBatchBuild tem exatamente uma lista de marcacao
	// e dois botoes, igual ao Choose Groups. Tentar distinguir por contagem de
	// controles ja falhou uma vez -- BatchBuild.xrc declara dois dialogos, e
	// contar o arquivo inteiro somava os controles dos dois.
	//
	// O que separa: o OnChooseGroups monta a lista de grupos e acrescenta
	// "Unassigned" depois do loop (BodySlideApp.cpp:3571), sem passar por _().
	// Ou seja, o ultimo item e sempre exatamente essa palavra, em qualquer
	// idioma. Uma lista de outfits nao termina assim.
	std::wstring lastItem = ListBoxItemText(listBox, count - 1);
	if (lastItem != L"Unassigned")
		return nullptr;

	return listBox;
}

std::wstring ListBoxItemText(HWND listBox, int index) {
	const int len = static_cast<int>(SendMessageW(listBox, LB_GETTEXTLEN, index, 0));
	if (len <= 0 || len > 4096)
		return std::wstring();

	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	int written = static_cast<int>(SendMessageW(listBox, LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data())));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

std::vector<std::wstring> ReadGroupNames(HWND listBox) {
	std::vector<std::wstring> names;
	const int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
	if (count <= 0)
		return names;

	for (int i = 0; i < count; ++i) {
		const int len = static_cast<int>(SendMessageW(listBox, LB_GETTEXTLEN, i, 0));
		if (len <= 0 || len > 4096)
			continue;
		std::wstring text(static_cast<size_t>(len) + 1, L'\0');
		int written = static_cast<int>(SendMessageW(listBox, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text.data())));
		if (written <= 0)
			continue;
		text.resize(static_cast<size_t>(written));
		names.push_back(text);
	}
	return names;
}

std::wstring TextOf(HWND hwnd) {
	const int len = GetWindowTextLengthW(hwnd);
	if (len <= 0)
		return std::wstring();
	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	int written = GetWindowTextW(hwnd, text.data(), len + 1);
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

// A caixa de filtro de grupos e um wxSearchCtrl anexado ao slot "searchHolder";
// o que interessa e o Edit nativo dentro dela.
//
// Procurar "o primeiro Edit da janela" nao serve, e nao por pouco: cada slider
// do BodySlide tem a sua caixinha de porcentagem, que tambem e um Edit. Numa
// janela com sliders carregados sao dezenas delas, todas ANTES das caixas de
// busca na varredura. O filtro acabava escrito dentro do campo de valor de um
// slider -- "MPP CBBE Reference, MPP HIMBO Reference" no lugar de "0%" -- e a
// lista de outfits, claro, nunca era filtrada.

// Texto de dica da caixa -- o "Filter groups..." apagado que aparece quando
// ela esta vazia. O BodySlide define um diferente para cada caixa
// (SetDescriptiveText), e no MSW isso vira o cue banner nativo.
std::wstring CueBannerOf(HWND edit) {
	wchar_t buffer[256] = {};
	if (!SendMessageW(edit, EM_GETCUEBANNER, reinterpret_cast<WPARAM>(buffer),
					  static_cast<LPARAM>(std::size(buffer))))
		return std::wstring();
	return std::wstring(buffer);
}

bool LooksLikeGroupCue(const std::wstring& cue) {
	// Heuristica em ingles de proposito, e so como desempate. O BodySlide
	// traduz esse texto, entao numa instalacao traduzida isto nao casa e a
	// escolha cai nos criterios seguintes -- nunca depende so disto.
	std::wstring lower;
	for (wchar_t c : cue)
		lower.push_back(static_cast<wchar_t>(std::towlower(c)));
	return lower.find(L"group") != std::wstring::npos;
}

void CollectEdits(HWND parent, std::vector<HWND>& out) {
	for (HWND child : ChildrenOf(parent)) {
		if (_wcsicmp(ClassOf(child).c_str(), L"Edit") == 0)
			out.push_back(child);
		CollectEdits(child, out);
	}
}

// Todo Edit da janela, sem limite de contagem.
//
// Havia um teto de 16 aqui. Com sliders carregados, os 16 primeiros sao todos
// caixas de porcentagem de slider, e a varredura parava antes de chegar em
// qualquer caixa de busca.
std::vector<HWND> AllEdits(HWND frame) {
	std::vector<HWND> edits;
	if (frame)
		CollectEdits(frame, edits);
	return edits;
}

bool ContentProvesGroupBox(HWND edit, const std::vector<std::wstring>& knownGroups) {
	if (knownGroups.empty())
		return false;

	const std::vector<std::wstring> tokens = ParseFilterTokens(TextOf(edit));
	if (tokens.empty())
		return false;

	for (const std::wstring& token : tokens) {
		if (std::find(knownGroups.begin(), knownGroups.end(), token) == knownGroups.end())
			return false;
	}
	return true;
}

HWND GroupFilterEditImpl(HWND frame, const std::vector<std::wstring>& knownGroups) {
	HWND byCue = nullptr;

	for (HWND edit : AllEdits(frame)) {
		// Conteudo e prova: so a caixa de grupos contem uma lista em que todo
		// token e um nome de grupo existente.
		if (ContentProvesGroupBox(edit, knownGroups))
			return edit;

		if (!byCue && LooksLikeGroupCue(CueBannerOf(edit)))
			byCue = edit;
	}

	// Nada provou e a dica nao ajudou. Devolver "o primeiro Edit" seria pior que
	// nao fazer nada: os primeiros sao campos de valor de slider, e escrever ali
	// estraga um valor do usuario em vez de filtrar coisa alguma.
	return byCue;
}

// Despeja no log as caixas que importam: as que tem dica -- as de busca -- mais
// a escolhida. Nao lista as dezenas de campos de slider, que so fariam volume.
void LogFilterCandidates(HWND frame, HWND chosen) {
	const std::vector<HWND> edits = AllEdits(frame);
	int withCue = 0;

	for (size_t i = 0; i < edits.size(); ++i) {
		const std::wstring cue = CueBannerOf(edits[i]);
		if (cue.empty() && edits[i] != chosen)
			continue;
		++withCue;
		LogF("group_search:   edit[%d] dica='%ls' texto='%ls'%s", static_cast<int>(i),
			 cue.c_str(), TextOf(edits[i]).c_str(),
			 edits[i] == chosen ? "  <== escolhida" : "");
	}

	LogF("group_search: %d edits na janela, %d com dica",
		 static_cast<int>(edits.size()), withCue);
}

void WriteFilter(HWND frame, const std::wstring& text) {
	HWND edit = GroupFilterEditImpl(frame, g_allGroups);
	LogFilterCandidates(frame, edit);
	if (!edit) {
		LogF("group_search: nao identifiquei a caixa de filtro de grupos -- nao escrevo "
			 "em lugar nenhum, para nao estragar o valor de algum slider");
		return;
	}

	// EM_SETSEL + EM_REPLACESEL em vez de WM_SETTEXT: WM_SETTEXT nao gera
	// EN_CHANGE, e e o EN_CHANGE que faz o wx disparar OnSearchChange ->
	// PopulateOutfitList. Substituir a selecao e exatamente o que acontece
	// quando o usuario digita.
	SendMessageW(edit, EM_SETSEL, 0, -1);
	SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
}

void HandleChooseGroupsDialog(HWND dlg, HWND listBox) {
	g_allGroups = ReadGroupNames(listBox);
	if (g_allGroups.empty()) {
		LogF("group_search: o listbox nao devolveu nomes -- deixando o dialogo original");
		return;
	}

	LogF("group_search: interceptado, %d grupos", static_cast<int>(g_allGroups.size()));

	// Estado inicial: parse da caixa de filtro, o mesmo que o OnChooseGroups faz.
	HWND edit = GroupFilterEditImpl(g_frame, g_allGroups);
	const std::vector<std::wstring> active = edit ? ParseFilterTokens(TextOf(edit)) : std::vector<std::wstring>();

	g_checked.assign(g_allGroups.size(), false);
	for (size_t i = 0; i < g_allGroups.size(); ++i)
		g_checked[i] = std::find(active.begin(), active.end(), g_allGroups[i]) != active.end();

	// Dispensa o dialogo do wx.
	//
	// EndDialog NAO serve aqui: ela e do dialog manager do Windows, e o
	// wxWidgets cria a janela com a classe #32770 mas roda o proprio loop
	// modal. Chamar EndDialog nao faz nada, e o dialogo original acaba
	// aparecendo depois do nosso. WM_CLOSE cai no wxDialog::OnCloseWindow,
	// que faz EndModal(wxID_CANCEL) -- o caminho de cancelamento de verdade.
	PostMessageW(dlg, WM_CLOSE, 0, 0);

	// Nosso dialogo so pode abrir depois que o loop modal do wx terminar,
	// senao ficariamos com dois modais aninhados. Por isso vai por mensagem
	// adiada, tratada no subclass do frame.
	if (g_showMsg)
		PostMessageW(g_frame, g_showMsg, 0, 0);
}

// Roda no frame, ja fora do loop modal do dialogo do wx.
void ShowAndApply() {
	// g_allGroups e g_checked sao estado global; dois dialogos ao mesmo tempo
	// escreveriam um por cima do outro. Nao deveria acontecer, mas a mensagem
	// que abre isto e postada, e mensagem postada pode chegar duas vezes.
	if (g_dialogOpen) {
		LogF("group_search: ja ha um dialogo aberto, ignorando");
		return;
	}

	g_dialogOpen = true;
	struct Guard {
		~Guard() { g_dialogOpen = false; }
	} guard;

	if (ShowOurDialog(g_frame) != IDOK) {
		LogF("group_search: cancelado");
		return;
	}

	std::vector<std::wstring> selected;
	for (size_t i = 0; i < g_allGroups.size(); ++i)
		if (g_checked[i])
			selected.push_back(g_allGroups[i]);

	WriteFilter(g_frame, JoinFilterTokens(selected));
	LogF("group_search: aplicado, %d grupos marcados", static_cast<int>(selected.size()));
}

LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
								   UINT_PTR, DWORD_PTR) {
	if (g_showMsg && msg == g_showMsg) {
		ShowAndApply();
		return 0;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

const UINT_PTR kPendingDialogId = 0xB508;

// Fica no dialogo do wx desde a criacao, so para pegar o momento em que ele
// tenta se exibir.
//
// Interceptar em HCBT_ACTIVATE era tarde demais: quando a ativacao chega, a
// janela ja foi mostrada e pintada, e o usuario via o dialogo original piscar
// antes do nosso. Aqui o proprio pedido de exibicao e desarmado, entao ele
// nunca chega a aparecer.
//
// Este tambem e o primeiro instante em que da para inspecionar o dialogo: os
// filhos ja existem, o que nao acontece em HCBT_CREATEWND.
LRESULT CALLBACK PendingDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
								   UINT_PTR id, DWORD_PTR) {
	if (msg == WM_WINDOWPOSCHANGING && !g_busy) {
		auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
		if (pos && (pos->flags & SWP_SHOWWINDOW)) {
			HWND listBox = ChooseGroupsListBoxImpl(hwnd);
			RemoveWindowSubclass(hwnd, PendingDialogProc, id);

			if (listBox) {
				pos->flags &= ~SWP_SHOWWINDOW; // nao aparece nem por um quadro
				g_busy = true;
				HandleChooseGroupsDialog(hwnd, listBox);
				g_busy = false;
			}
		}
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code != HCBT_CREATEWND || g_busy)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	HWND candidate = reinterpret_cast<HWND>(wParam);
	auto* created = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
	if (!candidate || !created || !created->lpcs)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	// Todo dialogo do frame entra na espera; quem decide se e o nosso e o
	// PendingDialogProc, quando os filhos ja existem.
	if (created->lpcs->hwndParent == g_frame && ClassOf(candidate) == L"#32770")
		SetWindowSubclass(candidate, PendingDialogProc, kPendingDialogId, 0);

	return CallNextHookEx(g_hook, code, wParam, lParam);
}

void InstallHere(void*) {
	g_hook = SetWindowsHookExW(WH_CBT, CbtProc, SelfModule(), GetCurrentThreadId());
	if (!g_hook) {
		LogF("group_search: SetWindowsHookEx(WH_CBT) falhou (erro %lu)", GetLastError());
		return;
	}
	if (!SetWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId, 0))
		LogF("group_search: SetWindowSubclass no frame falhou");
}

void UninstallHere(void*) {
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId);
}

bool Enabled(const Config& cfg) {
	return cfg.groupSearch;
}

} // namespace

// ---------------------------------------------------------------- logica pura

std::vector<std::wstring> ParseFilterTokens(const std::wstring& text) {
	std::vector<std::wstring> tokens;
	size_t start = 0;

	while (start <= text.size()) {
		size_t sep = text.find_first_of(L",;", start);
		std::wstring piece = TrimWide(text.substr(start, (sep == std::wstring::npos) ? std::wstring::npos : sep - start));
		if (!piece.empty())
			tokens.push_back(piece);
		if (sep == std::wstring::npos)
			break;
		start = sep + 1;
	}
	return tokens;
}

std::wstring JoinFilterTokens(const std::vector<std::wstring>& names) {
	std::wstring joined;
	for (size_t i = 0; i < names.size(); ++i) {
		if (i > 0)
			joined += L", ";
		joined += names[i];
	}
	return joined;
}

bool MatchesFilter(const std::wstring& name, const std::wstring& query) {
	if (query.empty())
		return true;
	return LowerWide(name).find(LowerWide(query)) != std::wstring::npos;
}

HWND FindGroupFilterEdit(HWND frame, const std::vector<std::wstring>& knownGroups) {
	return GroupFilterEditImpl(frame, knownGroups);
}

HWND FindChooseGroupsListBox(HWND dlg) {
	return ChooseGroupsListBoxImpl(dlg);
}

std::vector<BYTE> BuildGroupsDialogTemplate() {
	DialogTemplateBuilder tpl;
	const short cx = 260;
	const short cy = 240;
	const WORD itemCount = 8;

	// Sem DS_CENTER: ele centraliza no monitor, e o dialogo original do wx
	// centraliza na janela do BodySlide. O posicionamento e feito na mao no
	// WM_INITDIALOG para bater com o comportamento que o usuario ja conhece.
	tpl.BeginDialog(DS_MODALFRAME | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU,
					cx, cy, itemCount, L"Choose Groups");

	tpl.AddItem(WS_CHILD | WS_VISIBLE, 7, 6, cx - 14, 9, 0xFFFF, kAtomStatic,
				L"Choose groups to filter outfit list");
	tpl.AddItem(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
				7, 18, cx - 14, 13, kGroupSearchEdit, kAtomEdit, L"");
	tpl.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				7, 35, 62, 13, kGroupSearchCheckVisible, kAtomButton, L"Check visible");
	tpl.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				73, 35, 52, 13, kGroupSearchClearAll, kAtomButton, L"Clear all");
	tpl.AddCustomClass(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT |
						   LVS_NOCOLUMNHEADER | LVS_SINGLESEL,
					   7, 52, cx - 14, cy - 78, kGroupSearchList, WC_LISTVIEWW, L"");
	tpl.AddItem(WS_CHILD | WS_VISIBLE, 7, cy - 22, cx - 100, 9, kGroupSearchCounter, kAtomStatic, L"");
	tpl.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
				cx - 90, cy - 24, 40, 14, IDOK, kAtomButton, L"OK");
	tpl.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				cx - 46, cy - 24, 40, 14, IDCANCEL, kAtomButton, L"Cancel");

	return tpl.bytes;
}

namespace GroupSearch {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES};
	InitCommonControlsEx(&icc);

	if (!g_showMsg)
		g_showMsg = RegisterWindowMessageW(L"BSOSImprovements_ShowGroupSearch");

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
	g_allGroups.clear();
	g_checked.clear();
	g_visible.clear();
}

} // namespace GroupSearch

BSOS_REGISTER_FEATURE(groupsearch, "busca de grupos", HostApp::BodySlide, Enabled,
					  GroupSearch::Install, GroupSearch::Uninstall)
