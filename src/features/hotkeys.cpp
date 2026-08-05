#include "features/hotkeys.h"

#include <functional>
#include <string>
#include <vector>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/brush_resize.h"
#include "features/outfit_tree.h"
#include "features/registry.h"
#include "features/slider_menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

struct Binding {
	Hotkey key;
	std::string name;
	std::function<bool()> action; // true = consumiu a tecla
};

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
std::vector<Binding> g_bindings;
SliderMenuRefs g_sliderMenu;

bool ActionSelectReference() {
	bool selected = SelectReference(g_frame);
	LogF("atalho de reference: %s", selected ? "selecionado" : "projeto sem reference, ignorado");
	return selected;
}

// Os dois de slider so agem em edit mode. Fora dele devolvem false, e a tecla
// segue para a aplicacao em vez de ser engolida.
bool RunSliderCommand(const char* label, UINT commandId) {
	if (!IsSliderEditModeActive(g_frame, g_sliderMenu)) {
		LogF("%s: nenhum slider em edit mode (o submenu esta cinza), ignorado", label);
		return false;
	}

	const bool sent = InvokeMenuCommandId(g_frame, commandId);
	LogF("%s: %s comando id=%u", label, sent ? "enviado" : "FALHOU ao enviar", commandId);
	return sent;
}

bool ActionExportSliderObj() {
	return RunSliderCommand("export slider OBJ", g_sliderMenu.exportObjId);
}

bool ActionImportSliderObj() {
	return RunSliderCommand("import slider OBJ", g_sliderMenu.importObjId);
}

bool ActionBeginBrushResize() {
	if (BrushResize::IsActive())
		return true; // ja no modo: engole a tecla repetida
	POINT cursor = {};
	GetCursorPos(&cursor);
	BrushResize::Begin(cursor.x, cursor.y);
	return true;
}

// Registra evitando conflito: a primeira entrada com uma dada tecla vence.
void AddBinding(const Hotkey& key, const std::string& name, std::function<bool()> action) {
	if (!key.IsValid())
		return;

	for (const Binding& existing : g_bindings) {
		if (HotkeyMatches(existing.key, key.vk, key.shift, key.ctrl, key.alt)) {
			LogF("hotkeys: '%s' ignorado, a tecla ja pertence a '%s'", name.c_str(), existing.name.c_str());
			return;
		}
	}
	g_bindings.push_back({key, name, std::move(action)});
}

// Enquanto o modo de resize esta ligado, o mouse e as teclas de saida tem
// precedencia sobre qualquer atalho.
bool HandleBrushResizeMessage(MSG* msg) {
	if (!BrushResize::IsActive())
		return false;

	switch (msg->message) {
		case WM_MOUSEMOVE: {
			// Perdeu o foco no meio do arrasto (alt-tab): sai sem restaurar,
			// para nao mexer no brush pelas costas do usuario.
			HWND foreground = GetForegroundWindow();
			if (foreground != g_frame && !IsChild(g_frame, foreground)) {
				BrushResize::Confirm();
				return false;
			}

			// Reescreve a coordenada para a ancora e DEIXA a mensagem seguir.
			// Consumi-la e sintetizar outra no lugar mata o tratamento de
			// movimento do proprio Outfit Studio, que e quem redesenha a cena
			// -- foi assim que o circulo parou de mudar de tamanho na tela.
			BrushResize::RewriteMouseMove(msg);
			return false;
		}
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			BrushResize::Confirm();
			return true; // consome, senao o clique vira pincelada no mesh
		case WM_RBUTTONDOWN:
			BrushResize::Cancel();
			return true;
		case WM_KEYDOWN:
			if (msg->wParam == VK_ESCAPE) {
				BrushResize::Cancel();
				return true;
			}
			return false;
		default:
			return false;
	}
}

LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code != HC_ACTION || wParam != PM_REMOVE)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	MSG* msg = reinterpret_cast<MSG*>(lParam);

	if (HandleBrushResizeMessage(msg)) {
		msg->message = WM_NULL;
		msg->wParam = 0;
		msg->lParam = 0;
		return CallNextHookEx(g_hook, code, wParam, lParam);
	}

	if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	// Durante o arrasto de redimensionar, o modo e exclusivo: so as teclas que
	// o encerram valem. Sem isto, apertar B no meio do arrasto trocaria o shape
	// selecionado enquanto o brush ainda esta sendo ajustado.
	if (BrushResize::IsActive())
		return CallNextHookEx(g_hook, code, wParam, lParam);

	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	const UINT vk = static_cast<UINT>(msg->wParam);

	// Casar primeiro, checar as guardas depois. Assim o log consegue dizer que
	// a tecla certa chegou e o que a bloqueou, em vez de so silenciar.
	const Binding* match = nullptr;
	for (const Binding& binding : g_bindings) {
		if (HotkeyMatches(binding.key, vk, shift, ctrl, alt)) {
			match = &binding;
			break;
		}
	}
	if (!match)
		return CallNextHookEx(g_hook, code, wParam, lParam);

	// So dentro da janela principal. Com um dialogo modal aberto (arquivo,
	// propriedades) a tecla nao pode disparar comando da janela de tras.
	if (msg->hwnd != g_frame && !IsChild(g_frame, msg->hwnd)) {
		LogF("'%s': tecla chegou de uma janela de fora do frame (hwnd=%p), ignorada",
			 match->name.c_str(), static_cast<void*>(msg->hwnd));
		return CallNextHookEx(g_hook, code, wParam, lParam);
	}

	// Guarda obrigatoria: sem ela, digitar "B" no filtro de sliders viraria
	// atalho. O proprio OutfitStudio::CharHook do upstream toma esse cuidado.
	if (IsTextInputFocused()) {
		LogF("'%s': bloqueado, o foco esta num campo de texto (classe '%ls')",
			 match->name.c_str(), ClassOf(GetFocus()).c_str());
		return CallNextHookEx(g_hook, code, wParam, lParam);
	}

	if (!match->action())
		return CallNextHookEx(g_hook, code, wParam, lParam); // casou mas nao agiu

	// Consome, para o acelerador do wx tambem nao disparar.
	msg->message = WM_NULL;
	msg->wParam = 0;
	msg->lParam = 0;
	return CallNextHookEx(g_hook, code, wParam, lParam);
}

// Roda NA thread de UI, via RunOnUiThread.
void InstallHookHere(void*) {
	g_hook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, SelfModule(), GetCurrentThreadId());
	if (!g_hook)
		LogF("hotkeys: SetWindowsHookEx falhou na thread de UI (erro %lu)", GetLastError());
}

bool HotkeysEnabled(const Config& cfg) {
	return cfg.referenceHotkey || cfg.sliderObjHotkeys || cfg.brushResizeDrag || !cfg.remaps.empty();
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

	if (cfg.referenceHotkey)
		AddBinding(cfg.selectReference, "selecionar reference", ActionSelectReference);

	if (cfg.sliderObjHotkeys) {
		g_sliderMenu = ResolveSliderMenu(AppDir());
		if (g_sliderMenu.ok) {
			// Deixa registrado onde cada comando foi parar e como o menu esta
			// agora, para um unico teste bastar quando algo nao disparar.
			LogSliderMenuState(frame, g_sliderMenu);
		}
		if (BindSliderMenu(frame, g_sliderMenu)) {
			AddBinding(cfg.exportSliderObj, "export slider OBJ", ActionExportSliderObj);
			AddBinding(cfg.importSliderObj, "import slider OBJ", ActionImportSliderObj);
		}
	}

	if (cfg.brushResizeDrag && BrushResize::Install(frame))
		AddBinding(cfg.brushResize, "redimensionar brush", ActionBeginBrushResize);

	// [Remap]: qualquer tecla ligada a qualquer comando de menu. Resolvido
	// agora para nao pagar a leitura do XRC a cada tecla.
	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	for (const RemapEntry& entry : cfg.remaps) {
		MenuTrail path = ResolveMenuTrail(xrc.c_str(), entry.xrcName.c_str());
		if (path.empty()) {
			// Nome errado no INI nao pode derrubar os outros atalhos.
			LogF("remap: '%s' nao existe no XRC, ignorado", entry.xrcName.c_str());
			continue;
		}
		HWND frameCopy = frame;
		AddBinding(entry.key, "remap " + entry.xrcName,
				   [frameCopy, path]() { return InvokeMenuCommand(frameCopy, path); });
	}

	if (g_bindings.empty()) {
		LogF("hotkeys: nenhum atalho configurado");
		return false;
	}

	// Instalar da thread de bootstrap nao serve: hooks precisam da thread dona
	// da janela.
	if (!RunOnUiThread(frame, InstallHookHere, nullptr) || !g_hook) {
		g_bindings.clear();
		return false;
	}

	for (const Binding& binding : g_bindings)
		LogF("hotkeys: '%s' ligado", binding.name.c_str());
	return true;
}

void Uninstall() {
	if (g_hook) {
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
	}
	BrushResize::Uninstall();
	g_bindings.clear();
	g_frame = nullptr;
}

} // namespace Hotkeys

BSOS_REGISTER_FEATURE(hotkeys, "hotkeys", HostApp::OutfitStudio, HotkeysEnabled, Hotkeys::Install, Hotkeys::Uninstall)
