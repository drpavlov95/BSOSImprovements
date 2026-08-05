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
	refs.importSubmenu.path = {0, 2};    // o submenu de import, apos o separador
	refs.exportSubmenu.path = {0, 3};
	refs.importObjItem.path = {0, 2, 2}; // Import OBJ
	refs.exportObjItem.path = {0, 3, 2}; // Export OBJ
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
	TEST_ASSERT(!InvokeMenuCommand(frame, MenuTrail{}));
	TEST_ASSERT(!InvokeMenuCommand(frame, MenuTrail{{0, 99}, {}}));
	DrainMessages();
	TEST_ASSERT(g_lastCommand == 0);

	// refs invalido: sem resolucao completa do XRC o edit mode nunca conta como
	// ativo, para nao disparar o comando errado.
	SliderMenuRefs broken;
	TEST_ASSERT(!IsSliderEditModeActive(frame, broken));

	DestroyWindow(frame);
	return true;
}

// Outro mod inserindo item no menu VIVO.
//
// Caso real: o mod Drape injeta um separador e "Smart Conform All" no menu
// Slider do Outfit Studio. Nada disso existe no XRC, entao toda posicao lida
// do XRC dali para frente aponta dois itens antes do certo, e Shift+E/Shift+I
// paravam de funcionar sem dizer nada.
TEST(SurvivesAnotherModInjectingMenuItems) {
	HWND frame = MakeFrameWithSliderMenu();
	TEST_ASSERT(frame != nullptr);

	HMENU bar = GetMenu(frame);
	HMENU sliderMenu = SubMenuAt(bar, 0);
	TEST_ASSERT(sliderMenu != nullptr);

	InsertMenuW(sliderMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
	InsertMenuW(sliderMenu, 2, MF_BYPOSITION | MF_STRING, 9001,
				L"Smart Conform All\tCtrl+Alt+C");

	// Os submenus estavam em 2 e 3; agora estao em 4 e 5. O XRC continua
	// dizendo 2 e 3, porque o XRC nao sabe que alguem injetou nada.
	TEST_ASSERT(SubMenuAt(sliderMenu, 2) == nullptr); // aqui virou o item injetado

	MenuTrail importObj{{0, 2, 2}, {L"Slider", L"Import Slider Data", L"Import OBJ..."}};
	MenuTrail exportObj{{0, 3, 2}, {L"Slider", L"Export Slider Data", L"Export OBJ..."}};

	// Com o rotulo, os itens sao reencontrados onde de fato foram parar.
	TEST_ASSERT(MenuCommandId(frame, importObj) == 4003);
	TEST_ASSERT(MenuCommandId(frame, exportObj) == 5003);

	// Sem rotulo sobra a posicao crua, que e exatamente o bug: o caminho morre
	// no item injetado, que nao e submenu.
	MenuTrail blind = importObj;
	blind.labels.clear();
	TEST_ASSERT(MenuCommandId(frame, blind) == 0);

	// O edit mode e lido do submenu de import, que tambem andou de lugar.
	SliderMenuRefs refs;
	refs.importSubmenu = MenuTrail{{0, 2}, {L"Slider", L"Import Slider Data"}};
	refs.ok = true;

	EnableMenuItem(sliderMenu, 4, MF_BYPOSITION | MF_GRAYED);
	TEST_ASSERT(!IsSliderEditModeActive(frame, refs));
	EnableMenuItem(sliderMenu, 4, MF_BYPOSITION | MF_ENABLED);
	TEST_ASSERT(IsSliderEditModeActive(frame, refs));

	// Rotulo que nao existe no menu vivo -- instalacao traduzida -- volta a
	// valer a posicao do XRC, que e o comportamento antigo e nao uma quebra.
	MenuTrail translated{{0, 4, 2}, {L"Curseur", L"Importer", L"Importer OBJ..."}};
	TEST_ASSERT(MenuCommandId(frame, translated) == 4003);

	DestroyWindow(frame);
	return true;
}
