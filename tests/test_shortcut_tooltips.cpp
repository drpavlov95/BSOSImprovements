// De onde sai o atalho que vai para o tooltip, e a reescrita da notificacao.
#include "test_util.h"

#include <commctrl.h>

#include "features/shortcut_tooltips.h"

TEST(ReadsAcceleratorOutOfMenuLabels) {
	// O formato do Windows: nome, tab, acelerador. E o mesmo que o Outfit
	// Studio usa nos rotulos do XRC.
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Transform\tF") == L"F");
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Conform Selected\tCtrl+C") == L"Ctrl+C");
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Delete Slider\tCtrl+Del") == L"Ctrl+Del");
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Front\tShift+1") == L"Shift+1");

	// O mnemonico fica onde esta: ele pertence ao nome, nao ao acelerador.
	TEST_ASSERT(AcceleratorFromMenuLabel(L"&Undo\tCtrl+Z") == L"Ctrl+Z");

	// Sem tab nao ha acelerador -- e o caso da maioria dos itens.
	TEST_ASSERT(AcceleratorFromMenuLabel(L"New Project").empty());
	TEST_ASSERT(AcceleratorFromMenuLabel(L"").empty());

	// Tab sem nada util depois nao pode virar um "(  )" no tooltip.
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Algo\t").empty());
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Algo\t   ").empty());

	// So o primeiro tab separa; o resto e alinhamento e nao entra.
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Algo\tF\tlixo") == L"F");

	// Espaco em volta do acelerador nao pode vazar para dentro do parenteses.
	TEST_ASSERT(AcceleratorFromMenuLabel(L"Algo\t  Ctrl+K  ") == L"Ctrl+K");
	return true;
}

TEST(FormatsHotkeysTheWayMenusDo) {
	TEST_ASSERT(FormatHotkey(Hotkey{'K', false, false, false}) == L"K");
	TEST_ASSERT(FormatHotkey(Hotkey{'E', true, false, false}) == L"Shift+E");
	TEST_ASSERT(FormatHotkey(Hotkey{'C', false, true, false}) == L"Ctrl+C");
	TEST_ASSERT(FormatHotkey(Hotkey{'K', true, true, true}) == L"Ctrl+Alt+Shift+K");
	TEST_ASSERT(FormatHotkey(Hotkey{'3', false, false, false}) == L"3");

	// Hotkey nao configurada nao pode virar texto nenhum: um "()" vazio no fim
	// do tooltip seria pior que nada.
	TEST_ASSERT(FormatHotkey(Hotkey{}).empty());
	return true;
}

TEST(ComposesTooltipWithoutDuplicating) {
	TEST_ASSERT(ComposeTooltip(L"Shows a transform tool.", L"K") ==
				L"Shows a transform tool. (K)");

	// O ponto do teste: a notificacao pode passar por mais de uma subclasse
	// nossa -- a barra e o pai dela -- e sem idempotencia sairia "(K) (K)".
	TEST_ASSERT(ComposeTooltip(L"Shows a transform tool. (K)", L"K") ==
				L"Shows a transform tool. (K)");

	// Sem atalho, ou sem tooltip de origem, nada e inventado.
	TEST_ASSERT(ComposeTooltip(L"Undo a previous action.", L"") ==
				L"Undo a previous action.");
	TEST_ASSERT(ComposeTooltip(L"", L"K").empty());

	// Tooltip de varias linhas continua de varias linhas, com o atalho no fim.
	TEST_ASSERT(ComposeTooltip(L"Mask vertices.\nHold ALT to remove.", L"1") ==
				L"Mask vertices.\nHold ALT to remove. (1)");

	// Um sufixo parecido mas de outra tecla nao conta como ja aplicado.
	TEST_ASSERT(ComposeTooltip(L"Algo (F)", L"K") == L"Algo (F) (K)");
	return true;
}

namespace {

// Menubar de mentira com a mesma forma da do Outfit Studio: um menu de topo,
// um submenu dentro dele, itens com e sem acelerador.
HMENU BuildFakeMenuBar() {
	HMENU tools = CreatePopupMenu();
	AppendMenuW(tools, MF_STRING, 4321, L"Transform\tF");
	AppendMenuW(tools, MF_STRING, 4322, L"Pivot\tP");
	AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(tools, MF_STRING, 4323, L"Merge Vertex");

	HMENU brushes = CreatePopupMenu();
	AppendMenuW(brushes, MF_STRING, 4324, L"Inflate");

	AppendMenuW(tools, MF_POPUP, reinterpret_cast<UINT_PTR>(brushes), L"Current Tool");

	HMENU bar = CreateMenu();
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"Tool");
	return bar;
}

} // namespace

TEST(CollectsAcceleratorsFromTheWholeMenuBar) {
	HMENU bar = BuildFakeMenuBar();
	std::map<UINT, std::wstring> accel = CollectMenuAccelerators(bar);

	TEST_ASSERT(accel[4321] == L"F");
	TEST_ASSERT(accel[4322] == L"P");

	// Item sem acelerador nao entra no mapa: entrar com string vazia faria a
	// reescrita achar que ha atalho e produzir "()".
	TEST_ASSERT(accel.find(4323) == accel.end());

	// Item dentro de submenu tambem e alcancado -- os brushes do Outfit Studio
	// moram um nivel abaixo, em "Current Tool".
	TEST_ASSERT(accel.find(4324) == accel.end()); // esse nao tem acelerador
	TEST_ASSERT(accel.size() == 2);

	// Separador nao pode virar entrada de id 0.
	TEST_ASSERT(accel.find(0) == accel.end());

	DestroyMenu(bar);
	TEST_ASSERT(CollectMenuAccelerators(nullptr).empty());
	return true;
}

namespace {

HWND MakeFrameWithToolbar(HWND& outToolbar) {
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSTooltipHost";
		RegisterClassW(&wc);
		registered = true;
	}

	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
	InitCommonControlsEx(&icc);

	HWND frame = CreateWindowExW(0, L"BSOSTooltipHost", L"frame", WS_OVERLAPPEDWINDOW,
								 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!frame)
		return nullptr;

	SetMenu(frame, BuildFakeMenuBar());

	outToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
								 WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS,
								 0, 0, 400, 30, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!outToolbar)
		return frame;

	SendMessageW(outToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

	TBBUTTON buttons[2] = {};
	buttons[0].idCommand = 4321; // tem acelerador no menu
	buttons[0].fsState = TBSTATE_ENABLED;
	buttons[0].fsStyle = BTNS_BUTTON;
	buttons[1].idCommand = 4323; // nao tem
	buttons[1].fsState = TBSTATE_ENABLED;
	buttons[1].fsStyle = BTNS_BUTTON;
	SendMessageW(outToolbar, TB_ADDBUTTONS, 2, reinterpret_cast<LPARAM>(buttons));

	return frame;
}

// Simula o que o controle de tooltip manda quando o mouse para numa
// ferramenta, com o texto que o wx ja teria preenchido.
std::wstring AskTooltip(HWND target, UINT commandId, const wchar_t* wxText, UINT extraFlags = 0) {
	NMTTDISPINFOW info = {};
	info.hdr.hwndFrom = target;
	info.hdr.idFrom = commandId;
	info.hdr.code = TTN_GETDISPINFOW;
	info.uFlags = extraFlags;
	info.lpszText = const_cast<wchar_t*>(wxText);

	SendMessageW(target, WM_NOTIFY, static_cast<WPARAM>(commandId),
				 reinterpret_cast<LPARAM>(&info));

	if (!info.lpszText)
		return std::wstring(info.szText);
	return std::wstring(info.lpszText);
}

} // namespace

TEST(RewritesToolbarTooltipWithTheShortcut) {
	HWND toolbar = nullptr;
	HWND frame = MakeFrameWithToolbar(toolbar);
	TEST_ASSERT(frame != nullptr);
	TEST_ASSERT(toolbar != nullptr);

	TEST_ASSERT(ShortcutTooltips::Install(frame));

	// A ferramenta cujo comando tem acelerador no menu ganha o atalho, lido do
	// proprio menu -- e nada aqui precisou saber que 4321 e o Transform.
	TEST_ASSERT(AskTooltip(toolbar, 4321, L"Shows a transform tool.") ==
				L"Shows a transform tool. (F)");

	// A que nao tem acelerador passa intacta.
	TEST_ASSERT(AskTooltip(toolbar, 4323, L"Merges two vertices.") == L"Merges two vertices.");

	// Id desconhecido tambem passa intacto.
	TEST_ASSERT(AskTooltip(toolbar, 9999, L"Outra coisa.") == L"Outra coisa.");

	// Com TTF_IDISHWND o idFrom e um handle e nao um comando; casar assim
	// mesmo acertaria a ferramenta errada por coincidencia numerica.
	TEST_ASSERT(AskTooltip(toolbar, 4321, L"Transform.", TTF_IDISHWND) == L"Transform.");

	// Passar duas vezes -- barra e pai, que e o que acontece de verdade --
	// nao pode duplicar o sufixo.
	std::wstring twice = AskTooltip(toolbar, 4321, L"Transform. (F)");
	TEST_ASSERT(twice == L"Transform. (F)");

	ShortcutTooltips::Uninstall();

	// Depois de desinstalar, a notificacao volta a passar sem ser tocada.
	TEST_ASSERT(AskTooltip(toolbar, 4321, L"Transform.") == L"Transform.");

	DestroyWindow(frame);
	return true;
}
