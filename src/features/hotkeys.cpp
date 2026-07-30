#include "features/hotkeys.h"

#include <vector>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/outfit_tree.h"
#include "features/registry.h"
#include "features/slider_menu.h"
#include "win32/winfind.h"

namespace {

struct Binding {
	Hotkey key;
	const char* name;
	bool (*action)(); // true = consumiu a tecla
};

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
std::vector<Binding> g_bindings;

#ifdef BSOS_TRACE_KEYS
volatile LONG g_hookCalls = 0;
volatile LONG g_keyDowns = 0;
#define BSOS_COUNT_HOOK() InterlockedIncrement(&g_hookCalls)
#define BSOS_COUNT_KEY() InterlockedIncrement(&g_keyDowns)
#else
#define BSOS_COUNT_HOOK() ((void)0)
#define BSOS_COUNT_KEY() ((void)0)
#endif

SliderMenuRefs g_sliderMenu;

bool ActionSelectReference() {
	bool selected = SelectReference(g_frame);
	LogF("atalho de reference: %s", selected ? "selecionado" : "projeto sem reference, ignorado");
	return selected;
}

// Os dois abaixo so agem em edit mode. Fora dele devolvem false, e a tecla
// segue para a aplicacao em vez de ser engolida.
bool ActionExportSliderObj() {
	if (!IsSliderEditModeActive(g_frame, g_sliderMenu)) {
		LogF("atalho de export OBJ: fora do edit mode, ignorado");
		return false;
	}
	bool sent = InvokeMenuCommand(g_frame, g_sliderMenu.exportObjItem);
	LogF("atalho de export OBJ: %s", sent ? "comando enviado" : "falhou");
	return sent;
}

bool ActionImportSliderObj() {
	if (!IsSliderEditModeActive(g_frame, g_sliderMenu)) {
		LogF("atalho de import OBJ: fora do edit mode, ignorado");
		return false;
	}
	bool sent = InvokeMenuCommand(g_frame, g_sliderMenu.importObjItem);
	LogF("atalho de import OBJ: %s", sent ? "comando enviado" : "falhou");
	return sent;
}

LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
	BSOS_COUNT_HOOK();

	if (code != HC_ACTION || wParam != PM_REMOVE)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	MSG* msg = reinterpret_cast<MSG*>(lParam);
	if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	BSOS_COUNT_KEY();

	// So dentro da janela principal. Com um dialogo modal aberto (arquivo,
	// propriedades) a tecla nao pode disparar comando da janela de tras.
	if (msg->hwnd != g_frame && !IsChild(g_frame, msg->hwnd))
		return CallNextHookEx(g_hook, code, wParam, lParam);

	// Guarda obrigatoria: sem ela, digitar "B" no filtro de sliders viraria
	// atalho. O proprio OutfitStudio::CharHook do upstream toma esse cuidado.
	if (IsTextInputFocused())
		return CallNextHookEx(g_hook, code, wParam, lParam);

	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	const UINT vk = static_cast<UINT>(msg->wParam);

	for (const Binding& binding : g_bindings) {
		if (!HotkeyMatches(binding.key, vk, shift, ctrl, alt))
			continue;
		if (!binding.action())
			break; // casou mas nao agiu: deixa a tecla seguir para o app

		// Consome, para o acelerador do wx tambem nao disparar.
		msg->message = WM_NULL;
		msg->wParam = 0;
		msg->lParam = 0;
		break;
	}

	return CallNextHookEx(g_hook, code, wParam, lParam);
}

// Roda NA thread de UI, via RunOnUiThread.
void InstallHookHere(void*) {
	g_hook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, SelfModule(), GetCurrentThreadId());
	if (!g_hook)
		LogF("hotkeys: SetWindowsHookEx falhou na thread de UI (erro %lu)", GetLastError());
}

bool HotkeysEnabled(const Config& cfg) {
	return cfg.referenceHotkey || cfg.sliderObjHotkeys;
}

} // namespace

bool HotkeyMatches(const Hotkey& hk, UINT vk, bool shift, bool ctrl, bool alt) {
	if (!hk.IsValid() || hk.vk != vk)
		return false;
	return hk.shift == shift && hk.ctrl == ctrl && hk.alt == alt;
}

namespace Hotkeys {

bool Install(HWND frame) {
	Uninstall();

	g_frame = frame;
	const Config& cfg = Cfg();

	if (cfg.referenceHotkey && cfg.selectReference.IsValid())
		g_bindings.push_back({cfg.selectReference, "selecionar reference", ActionSelectReference});

	if (cfg.sliderObjHotkeys) {
		g_sliderMenu = ResolveSliderMenu(AppDir());
		if (g_sliderMenu.ok) {
			if (cfg.exportSliderObj.IsValid())
				g_bindings.push_back({cfg.exportSliderObj, "export slider OBJ", ActionExportSliderObj});
			if (cfg.importSliderObj.IsValid())
				g_bindings.push_back({cfg.importSliderObj, "import slider OBJ", ActionImportSliderObj});
		}
	}

	if (g_bindings.empty()) {
		LogF("hotkeys: nenhum atalho configurado");
		return false;
	}

	// Instalar da thread de bootstrap nao serve: hooks e subclasses precisam
	// da thread dona da janela.
	if (!RunOnUiThread(frame, InstallHookHere, nullptr) || !g_hook) {
		g_bindings.clear();
		return false;
	}

	for (const Binding& binding : g_bindings)
		LogF("hotkeys: '%s' ligado", binding.name);

#ifdef BSOS_TRACE_KEYS
	CreateThread(
		nullptr, 0,
		[](LPVOID) -> DWORD {
			for (int i = 0; i < 20; ++i) {
				Sleep(1000);
				LogF("diag: chamadas=%ld keydowns=%ld", g_hookCalls, g_keyDowns);
			}
			return 0;
		},
		nullptr, 0, nullptr);
#endif

	return true;
}

void Uninstall() {
	if (g_hook) {
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
	}
	g_bindings.clear();
	g_frame = nullptr;
}

} // namespace Hotkeys

BSOS_REGISTER_FEATURE(hotkeys, "hotkeys", HostApp::OutfitStudio, HotkeysEnabled, Hotkeys::Install, Hotkeys::Uninstall)
