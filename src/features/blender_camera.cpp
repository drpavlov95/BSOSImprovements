#include "features/blender_camera.h"

#include <commctrl.h>

#include <string>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "win32/menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

const UINT_PTR kFrameSubclassId = 0xB50E;

HWND g_frame = nullptr;
HWND g_canvas = nullptr;
HMENU g_viewMenu = nullptr;
UINT g_commandId = 0;
bool g_enabled = false;
bool g_installed = false;
CameraState g_state;

bool MenuUsesId(HMENU menu, UINT id, int depth) {
	if (!menu || depth > 8)
		return false;

	const int count = GetMenuItemCount(menu);
	for (int i = 0; i < count; ++i) {
		if (HMENU sub = GetSubMenu(menu, i)) {
			if (MenuUsesId(sub, id, depth + 1))
				return true;
			continue;
		}
		if (CommandIdAt(menu, i) == id)
			return true;
	}
	return false;
}

// Um id que nao colida com nada que ja esteja na menubar.
//
// Nao da para escolher um numero fixo e torcer: os ids do wx sao atribuidos em
// runtime pela ordem de registro do XRC, entao nao ha faixa reservada que se
// possa assumir livre. Verificar contra o menu vivo custa uma varredura, uma
// vez, e elimina a duvida.
UINT FindFreeCommandId(HMENU bar) {
	for (UINT candidate = 0xBF20; candidate < 0xBF60; ++candidate) {
		if (!MenuUsesId(bar, candidate, 0))
			return candidate;
	}
	return 0;
}

HMENU FindViewMenu(HWND frame) {
	HMENU bar = GetMenu(frame);
	if (!bar)
		return nullptr;

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	const MenuTrail trail = ResolveMenuTrail(xrc.c_str(), "menuView");
	if (trail.empty())
		return nullptr;

	int index = -1;
	HMENU container = ContainerAtLabeledPath(bar, trail.path, trail.labels, index);
	return container ? SubMenuAt(container, index) : nullptr;
}

void UpdateCheckMark() {
	if (g_viewMenu && g_commandId)
		CheckMenuItem(g_viewMenu, g_commandId,
					  MF_BYCOMMAND | (g_enabled ? MF_CHECKED : MF_UNCHECKED));
}

LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, FrameSubclassProc, id);

	if (msg == WM_COMMAND && g_commandId != 0 && LOWORD(wParam) == g_commandId &&
		HIWORD(wParam) == 0 && lParam == 0) {
		BlenderCamera::SetEnabled(!g_enabled);
		LogF("camera blender: %s pelo menu", g_enabled ? "ligada" : "desligada");
		return 0; // nosso comando: o wx nao tem o que fazer com ele
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SubclassHere(void*) {
	if (!SetWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId, 0))
		LogF("camera blender: nao consegui subclassar o frame -- fica so o INI");
}

void UnsubclassHere(void*) {
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId);
}

} // namespace

bool DragWasReleasedOutside(const CameraState& state, UINT message, bool middleDown) {
	if (!state.orbiting && !state.panning && !state.zooming)
		return false;
	if (message != WM_MOUSEMOVE)
		return false;
	return !middleDown;
}

bool TranslateCameraMessage(CameraState& state, UINT& message, WPARAM& wParam) {
	switch (message) {
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK: {
			// Ctrl+meio e o zoom do Blender. No Outfit Studio o zoom fino e
			// Shift+meio, entao um modificador vira o outro.
			if (wParam & MK_CONTROL) {
				state.zooming = true;
				wParam &= ~static_cast<WPARAM>(MK_CONTROL);
				wParam |= MK_SHIFT;
				return true;
			}

			// Shift+meio e o pan do Blender. No Outfit Studio o pan e o meio
			// PURO -- com Shift ele faz zoom -- entao o Shift precisa sair.
			if (wParam & MK_SHIFT) {
				state.panning = true;
				wParam &= ~static_cast<WPARAM>(MK_SHIFT);
				return true;
			}

			state.orbiting = true;
			message = (message == WM_MBUTTONDOWN) ? WM_RBUTTONDOWN : WM_RBUTTONDBLCLK;
			wParam &= ~static_cast<WPARAM>(MK_MBUTTON);
			wParam |= MK_RBUTTON;
			return true;
		}

		case WM_MBUTTONUP:
			if (state.orbiting) {
				state.orbiting = false;
				message = WM_RBUTTONUP;
				// Nenhum bit a mexer: numa mensagem de soltar, o Windows ja
				// entrega o botao fora da mascara.
				return true;
			}
			if (state.panning) {
				state.panning = false;
				wParam &= ~static_cast<WPARAM>(MK_SHIFT);
				return true;
			}
			if (state.zooming) {
				state.zooming = false;
				wParam &= ~static_cast<WPARAM>(MK_CONTROL);
				wParam |= MK_SHIFT;
				return true;
			}
			return false;

		case WM_MOUSEMOVE:
			// Durante o arrasto o programa pode consultar a mascara em vez de
			// lembrar do aperto. Cobrir os dois casos custa dois bits.
			if (state.orbiting) {
				wParam &= ~static_cast<WPARAM>(MK_MBUTTON);
				wParam |= MK_RBUTTON;
				return true;
			}
			if (state.panning) {
				wParam &= ~static_cast<WPARAM>(MK_SHIFT);
				return true;
			}
			if (state.zooming) {
				wParam &= ~static_cast<WPARAM>(MK_CONTROL);
				wParam |= MK_SHIFT;
				return true;
			}
			return false;

		default:
			return false;
	}
}

namespace BlenderCamera {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;
	g_enabled = Cfg().blenderCamera;
	g_state = CameraState();

	g_canvas = FindLargestVisibleByClass(frame, L"wxGLCanvas");
	if (!g_canvas) {
		LogF("camera blender: nenhum wxGLCanvas visivel, feature desligada");
		return false;
	}

	// O item de menu e um extra: se nao der para criar, a feature continua
	// funcionando pelo INI.
	HMENU bar = GetMenu(frame);
	g_viewMenu = FindViewMenu(frame);
	g_commandId = bar ? FindFreeCommandId(bar) : 0;

	if (g_viewMenu && g_commandId) {
		AppendMenuW(g_viewMenu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(g_viewMenu, MF_STRING, g_commandId, L"Blender camera");
		UpdateCheckMark();
		DrawMenuBar(frame);
		RunOnUiThread(frame, SubclassHere, nullptr);
		LogF("camera blender: item de menu criado com id %u", g_commandId);
	} else {
		g_viewMenu = nullptr;
		g_commandId = 0;
		LogF("camera blender: sem item de menu (menu View nao resolvido) -- so o INI");
	}

	g_installed = true;
	LogF("camera blender: instalada no canvas %p, comeca %s",
		 static_cast<void*>(g_canvas), g_enabled ? "ligada" : "desligada");
	return true;
}

void Uninstall() {
	if (g_installed && g_frame) {
		if (g_viewMenu && g_commandId) {
			DeleteMenu(g_viewMenu, g_commandId, MF_BYCOMMAND);

			// E o separador que veio junto. Deixa-lo para tras faria o menu do
			// usuario ganhar um risco solto no fim a cada ciclo de instalar e
			// desinstalar.
			const int last = GetMenuItemCount(g_viewMenu) - 1;
			if (last >= 0 && MenuTextAt(g_viewMenu, last).empty() &&
				!GetSubMenu(g_viewMenu, last))
				DeleteMenu(g_viewMenu, static_cast<UINT>(last), MF_BYPOSITION);

			if (IsWindow(g_frame))
				DrawMenuBar(g_frame);
		}
		RunOnUiThread(g_frame, UnsubclassHere, nullptr);
	}

	g_frame = nullptr;
	g_canvas = nullptr;
	g_viewMenu = nullptr;
	g_commandId = 0;
	g_installed = false;
	g_state = CameraState();
}

bool IsEnabled() {
	return g_enabled;
}

void SetEnabled(bool enabled) {
	g_enabled = enabled;
	// Sair no meio de um arrasto deixaria o estado preso em "orbitando", e a
	// partir dai todo movimento do mouse viria com o botao trocado.
	g_state = CameraState();
	UpdateCheckMark();
}

// Fecha um arrasto que ficou preso, mandando ao programa o "soltar" que ele
// esta esperando. Sem esse fechamento o Outfit Studio continuaria achando que o
// botao direito esta pressionado, mesmo com o nosso estado ja limpo.
void EndStuckDrag() {
	const UINT closing = g_state.orbiting ? WM_RBUTTONUP : WM_MBUTTONUP;
	g_state = CameraState();

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(g_canvas, &cursor);

	// Postada e nao enviada: estamos dentro do hook de mensagens, e a ordem
	// natural e esta chegar depois da mensagem que esta sendo tratada agora.
	PostMessageW(g_canvas, closing, 0,
				 MAKELPARAM(static_cast<WORD>(cursor.x), static_cast<WORD>(cursor.y)));
	LogF("camera blender: o botao foi solto fora da janela, arrasto encerrado");
}

void RewriteMouseMessage(MSG* msg) {
	if (!g_enabled || !msg || !g_canvas)
		return;

	// Fora da view 3D nada e traduzido -- MENOS o fim de um arrasto que ja
	// comecou. Se o botao for solto com o ponteiro em outra janela e essa
	// mensagem for ignorada, o estado fica preso em "orbitando" para sempre.
	const bool dragging = g_state.orbiting || g_state.panning || g_state.zooming;
	if (msg->hwnd != g_canvas && !dragging)
		return;

	// O caso em que nem a mensagem de soltar chega: ela foi para outro
	// processo. Ai o unico sinal e a tecla nao estar mais apertada.
	if (DragWasReleasedOutside(g_state, msg->message,
							   (GetKeyState(VK_MBUTTON) & 0x8000) != 0)) {
		EndStuckDrag();
		return; // a mensagem atual segue intacta: o botao ja nao esta apertado
	}

	UINT message = msg->message;
	WPARAM wParam = msg->wParam;
	if (!TranslateCameraMessage(g_state, message, wParam))
		return;

	msg->message = message;
	msg->wParam = wParam;
}

} // namespace BlenderCamera
