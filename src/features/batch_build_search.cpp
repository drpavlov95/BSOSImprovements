#include "features/batch_build_search.h"

#include <commctrl.h>

#include <cwctype>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/group_search.h" // MatchesFilter
#include "features/registry.h"
#include "win32/winfind.h"

namespace {

const int kIdSearch = 0xBF01;
const int kIdToggle = 0xBF02;
const UINT_PTR kDialogSubclassId = 0xB509;
const UINT_PTR kEditSubclassId = 0xB50A;

// Altura da faixa reservada acima da lista, em pixels.
const int kBandHeight = 26;

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
HWND g_dialog = nullptr;
HWND g_list = nullptr;
std::vector<std::wstring> g_items;
int g_lastMatch = -1;

std::wstring ListItemText(HWND listBox, int index) {
	const int len = static_cast<int>(SendMessageW(listBox, LB_GETTEXTLEN, index, 0));
	if (len <= 0 || len > 4096)
		return std::wstring();
	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	int written = static_cast<int>(SendMessageW(listBox, LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data())));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

std::wstring SearchText() {
	HWND edit = GetDlgItem(g_dialog, kIdSearch);
	if (!edit)
		return std::wstring();
	wchar_t raw[256] = {};
	GetWindowTextW(edit, raw, 256);
	return raw;
}

// Rola ate um item e o deixa selecionado. Nao mexe na marcacao.
void RevealItem(int index) {
	if (index < 0)
		return;
	SendMessageW(g_list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
	SendMessageW(g_list, LB_SETTOPINDEX, static_cast<WPARAM>(index), 0);
	g_lastMatch = index;
}

void JumpToMatch(bool fromCurrent) {
	const std::wstring query = SearchText();
	if (query.empty())
		return;

	const int count = static_cast<int>(g_items.size());
	const int start = fromCurrent ? (g_lastMatch + 1) : 0;

	// Da a volta na lista, para Enter repetido percorrer todos os resultados.
	for (int step = 0; step < count; ++step) {
		const int i = (start + step) % count;
		if (MatchesFilter(g_items[i], query)) {
			RevealItem(i);
			return;
		}
	}
}

// Alterna a marcacao dos itens que casam.
//
// Vai por tecla, e nao escrevendo estado: o wxCheckListBox guarda a marcacao
// em estrutura propria, invisivel de fora. Selecionar o item e mandar espaco
// faz o proprio wx alternar, entao o estado interno dele nunca sai de sincronia
// com o que aparece na tela.
void ToggleMatching() {
	const std::wstring query = SearchText();
	if (query.empty()) {
		LogF("batch_build: busca vazia, nada a alternar");
		return;
	}

	int toggled = 0;
	for (int i = 0; i < static_cast<int>(g_items.size()); ++i) {
		if (!MatchesFilter(g_items[i], query))
			continue;

		SendMessageW(g_list, LB_SETCURSEL, static_cast<WPARAM>(i), 0);
		SendMessageW(g_list, WM_KEYDOWN, VK_SPACE, 0);
		SendMessageW(g_list, WM_CHAR, VK_SPACE, 0);
		SendMessageW(g_list, WM_KEYUP, VK_SPACE, 0);
		++toggled;
	}

	LogF("batch_build: alternados %d itens que casam com '%ls'", toggled, query.c_str());
}

// Reaplica a nossa faixa depois que o wx faz o layout dele.
//
// O dialogo tem wxRESIZE_BORDER, entao o wx refaz o layout a cada WM_SIZE e
// devolve a lista ao tamanho cheio. Recalcular sempre a partir do retangulo
// que ele acabou de definir mantem isso estavel e nao acumula.
void ApplyLayout() {
	HWND edit = GetDlgItem(g_dialog, kIdSearch);
	HWND toggle = GetDlgItem(g_dialog, kIdToggle);
	if (!g_list || !edit || !toggle)
		return;

	RECT lr = {};
	GetWindowRect(g_list, &lr);
	MapWindowPoints(nullptr, g_dialog, reinterpret_cast<POINT*>(&lr), 2);

	const int width = lr.right - lr.left;
	const int height = lr.bottom - lr.top;
	if (height <= kBandHeight)
		return;

	const int toggleWidth = 130;
	SetWindowPos(g_list, nullptr, lr.left, lr.top + kBandHeight, width, height - kBandHeight,
				 SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(edit, nullptr, lr.left, lr.top + 1, width - toggleWidth - 6, kBandHeight - 5,
				 SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(toggle, nullptr, lr.left + width - toggleWidth, lr.top, toggleWidth, kBandHeight - 3,
				 SWP_NOZORDER | SWP_NOACTIVATE);
}

// Enter dentro da caixa de busca dispararia o botao padrao do dialogo, que e
// OK -- ou seja, fecharia o dialogo e comecaria o build. Aqui ele vira "proximo
// resultado".
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	if (msg == WM_GETDLGCODE)
		return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wParam, lParam);

	if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
		JumpToMatch(true);
		return 0;
	}
	// Esc segue o caminho normal e fecha o dialogo, como o usuario espera.
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK DialogSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	switch (msg) {
		case WM_COMMAND:
			if (LOWORD(wParam) == kIdSearch && HIWORD(wParam) == EN_CHANGE) {
				g_lastMatch = -1;
				JumpToMatch(false);
				return 0;
			}
			if (LOWORD(wParam) == kIdToggle && HIWORD(wParam) == BN_CLICKED) {
				ToggleMatching();
				return 0;
			}
			break;

		case WM_SIZE: {
			// Deixa o wx posicionar primeiro, depois reserva a faixa.
			LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
			ApplyLayout();
			return r;
		}

		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, DialogSubclassProc, id);
			g_dialog = nullptr;
			g_list = nullptr;
			g_items.clear();
			break;

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void AddSearchControls(HWND dlg) {
	HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
	HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dlg, GWLP_HINSTANCE));

	HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
								WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
								0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdSearch)), inst, nullptr);
	HWND toggle = CreateWindowExW(0, L"BUTTON", L"Toggle matching",
								  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
								  0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdToggle)), inst, nullptr);
	if (!edit || !toggle) {
		LogF("batch_build: nao consegui criar os controles de busca");
		return;
	}

	if (font) {
		SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
		SendMessageW(toggle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	}
	SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search outfits..."));
	SetWindowSubclass(edit, EditSubclassProc, kEditSubclassId, 0);
}

void HandleBatchBuildDialog(HWND dlg, HWND listBox) {
	g_dialog = dlg;
	g_list = listBox;
	g_lastMatch = -1;

	const int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
	g_items.clear();
	g_items.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
		g_items.push_back(ListItemText(listBox, i));

	AddSearchControls(dlg);
	SetWindowSubclass(dlg, DialogSubclassProc, kDialogSubclassId, 0);
	ApplyLayout();

	LogF("batch_build: busca instalada, %d outfits", count);
}

const UINT_PTR kPendingId = 0xB50B;

LRESULT CALLBACK PendingDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_WINDOWPOSCHANGING) {
		auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
		if (pos && (pos->flags & SWP_SHOWWINDOW)) {
			// Neste ponto os filhos ja existem. Diferente do Choose Groups, o
			// dialogo NAO e escondido: ele continua sendo o dono da selecao.
			HWND listBox = FindBatchBuildListBox(hwnd);
			RemoveWindowSubclass(hwnd, PendingDialogProc, id);
			if (listBox)
				HandleBatchBuildDialog(hwnd, listBox);
		}
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code != HCBT_CREATEWND)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	HWND candidate = reinterpret_cast<HWND>(wParam);
	auto* created = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
	if (!candidate || !created || !created->lpcs)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	if (created->lpcs->hwndParent == g_frame && ClassOf(candidate) == L"#32770")
		SetWindowSubclass(candidate, PendingDialogProc, kPendingId, 0);

	return CallNextHookEx(g_hook, code, wParam, lParam);
}

void InstallHere(void*) {
	g_hook = SetWindowsHookExW(WH_CBT, CbtProc, SelfModule(), GetCurrentThreadId());
	if (!g_hook)
		LogF("batch_build: SetWindowsHookEx(WH_CBT) falhou (erro %lu)", GetLastError());
}

bool Enabled(const Config& cfg) {
	return cfg.batchBuildSearch;
}

} // namespace

std::vector<int> MatchingItems(const std::vector<std::wstring>& items, const std::wstring& query) {
	std::vector<int> hits;
	if (query.empty())
		return hits;
	for (int i = 0; i < static_cast<int>(items.size()); ++i)
		if (MatchesFilter(items[i], query))
			hits.push_back(i);
	return hits;
}

HWND FindBatchBuildListBox(HWND dlg) {
	HWND listBox = nullptr;
	int listBoxes = 0, buttons = 0, edits = 0, combos = 0;

	for (HWND child : ChildrenOf(dlg)) {
		std::wstring cls = ClassOf(child);
		if (_wcsicmp(cls.c_str(), L"ListBox") == 0) {
			++listBoxes;
			listBox = child;
		} else if (_wcsicmp(cls.c_str(), L"Button") == 0) {
			++buttons;
		} else if (_wcsicmp(cls.c_str(), L"Edit") == 0) {
			++edits;
		} else if (_wcsicmp(cls.c_str(), L"ComboBox") == 0) {
			++combos;
		}
	}

	// Contando POR DIALOGO (nao por arquivo -- esse erro ja custou caro), a
	// combinacao "1 lista, 2 botoes, 0 campos de texto, 0 combos" pertence
	// apenas ao dlgBatchBuild e ao Choose Groups entre todos os dialogos do
	// BodySlide. dlgSavePreset e dlgSliderProp tem campo de texto,
	// dlgSettings tem combos, dlgSliderDataImport tem duas listas,
	// dlgPackProjects tem quatro botoes, dlgGroupManager tem tres listas.
	if (listBoxes != 1 || buttons != 2 || edits != 0 || combos != 0 || !listBox)
		return nullptr;

	const LONG style = GetWindowLongW(listBox, GWL_STYLE);
	if ((style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) == 0)
		return nullptr;

	const int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
	if (count <= 0)
		return nullptr;

	// E o que separa os dois: o Choose Groups sempre termina em "Unassigned",
	// acrescentado sem traducao depois do loop de grupos. Uma lista de outfits
	// nao termina assim.
	if (ListItemText(listBox, count - 1) == L"Unassigned")
		return nullptr;

	return listBox;
}

namespace BatchBuildSearch {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	if (!RunOnUiThread(frame, InstallHere, nullptr))
		return false;
	return g_hook != nullptr;
}

void Uninstall() {
	if (g_hook) {
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
	}
	g_frame = nullptr;
	g_dialog = nullptr;
	g_list = nullptr;
	g_items.clear();
}

} // namespace BatchBuildSearch

BSOS_REGISTER_FEATURE(batchbuild, "busca no batch build", HostApp::BodySlide, Enabled,
					  BatchBuildSearch::Install, BatchBuildSearch::Uninstall)
