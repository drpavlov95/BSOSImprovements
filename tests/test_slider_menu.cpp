// Ler o edit mode do estado do menu e disparar o comando certo.
#include "test_util.h"

#include "features/slider_menu.h"
#include "win32/menu.h"

namespace {

UINT g_lastCommand = 0;

LRESULT CALLBACK RecordingWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_COMMAND)
		g_lastCommand = LOWORD(wp);
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// Espelha a estrutura do Outfit Studio:
//   barra[0] = menu "Slider"
//     [0] item comum
//     [1] separador                      <- posicao ocupada
//     [2] submenu "Import Slider Data"
//           [0] NIF  [1] BSD  [2] OBJ
//     [3] submenu "Export Slider Data"
//           [0] NIF  [1] BSD  [2] OBJ
HWND MakeFrameWithSliderMenu() {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = RecordingWndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSMenuHost";
		RegisterClassW(&wc);
		ready = true;
	}

	HMENU importMenu = CreatePopupMenu();
	AppendMenuW(importMenu, MF_STRING, 4001, L"Import NIF...");
	AppendMenuW(importMenu, MF_STRING, 4002, L"Import BSD...");
	AppendMenuW(importMenu, MF_STRING, 4003, L"Import OBJ...");

	HMENU exportMenu = CreatePopupMenu();
	AppendMenuW(exportMenu, MF_STRING, 5001, L"Export NIF...");
	AppendMenuW(exportMenu, MF_STRING, 5002, L"Export BSD...");
	AppendMenuW(exportMenu, MF_STRING, 5003, L"Export OBJ...");

	HMENU sliderMenu = CreatePopupMenu();
	AppendMenuW(sliderMenu, MF_STRING, 3001, L"New Slider");
	AppendMenuW(sliderMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(sliderMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(importMenu), L"Import Slider Data");
	AppendMenuW(sliderMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(exportMenu), L"Export Slider Data");

	HMENU bar = CreateMenu();
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sliderMenu), L"Slider");

	return CreateWindowExW(0, L"BSOSMenuHost", L"frame", WS_OVERLAPPEDWINDOW,
						   0, 0, 400, 300, nullptr, bar, GetModuleHandleW(nullptr), nullptr);
}

void DrainMessages() {
	MSG msg;
	for (int i = 0; i < 100 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

} // namespace

TEST(ReadsEditModeFromMenuStateAndPostsCommand) {
	HWND frame = MakeFrameWithSliderMenu();
	TEST_ASSERT(frame != nullptr);

	SliderMenuRefs refs;
	refs.importSubmenu = {0, 2};    // o submenu de import, apos o separador
	refs.exportSubmenu = {0, 3};
	refs.importObjItem = {0, 2, 2}; // Import OBJ
	refs.exportObjItem = {0, 3, 2}; // Export OBJ
	refs.ok = true;

	HMENU bar = GetMenu(frame);
	HMENU sliderMenu = SubMenuAt(bar, 0);
	TEST_ASSERT(sliderMenu != nullptr);

	// Fora do edit mode o Outfit Studio deixa os submenus cinzas.
	EnableMenuItem(sliderMenu, 2, MF_BYPOSITION | MF_GRAYED);
	EnableMenuItem(sliderMenu, 3, MF_BYPOSITION | MF_GRAYED);
	TEST_ASSERT(!IsSliderEditModeActive(frame, refs));

	// MenuEnterSliderEdit os habilita.
	EnableMenuItem(sliderMenu, 2, MF_BYPOSITION | MF_ENABLED);
	EnableMenuItem(sliderMenu, 3, MF_BYPOSITION | MF_ENABLED);
	TEST_ASSERT(IsSliderEditModeActive(frame, refs));

	// O caminho precisa chegar no item de OBJ, nao no de NIF nem no de BSD.
	TEST_ASSERT(MenuCommandId(frame, refs.importObjItem) == 4003);
	TEST_ASSERT(MenuCommandId(frame, refs.exportObjItem) == 5003);

	// Os ids sao lidos uma vez na instalacao e reusados. Reler a cada tecla se
	// mostrou pouco confiavel no app real: funcionava na inicializacao e
	// devolvia zero na hora de usar.
	TEST_ASSERT(BindSliderMenu(frame, refs));
	TEST_ASSERT(refs.importObjId == 4003);
	TEST_ASSERT(refs.exportObjId == 5003);

	g_lastCommand = 0;
	TEST_ASSERT(InvokeMenuCommandId(frame, refs.importObjId));
	DrainMessages();
	TEST_ASSERT(g_lastCommand == 4003);

	g_lastCommand = 0;
	TEST_ASSERT(SendMenuCommandId(frame, refs.exportObjId));
	TEST_ASSERT(g_lastCommand == 5003); // sincrono: ja chegou

	// Id zero ou caminho invalido nao podem postar nada.
	g_lastCommand = 0;
	TEST_ASSERT(!InvokeMenuCommandId(frame, 0));
	TEST_ASSERT(!InvokeMenuCommand(frame, MenuPath{}));
	TEST_ASSERT(!InvokeMenuCommand(frame, MenuPath{0, 99}));
	DrainMessages();
	TEST_ASSERT(g_lastCommand == 0);

	// refs invalido: sem resolucao completa do XRC o edit mode nunca conta como
	// ativo, para nao disparar o comando errado.
	SliderMenuRefs broken;
	TEST_ASSERT(!IsSliderEditModeActive(frame, broken));

	DestroyWindow(frame);
	return true;
}
