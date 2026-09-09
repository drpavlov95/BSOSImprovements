#include "features/blender_camera.h"

#include <commctrl.h>

#include <string>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "win32/menu.h"
#include "win32/menu_toggle.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {


HWND g_frame = nullptr;
HWND g_canvas = nullptr;
UINT g_commandId = 0;
bool g_enabled = false;
bool g_installed = false;
CameraState g_state;

// O Shift que nos mesmos apertamos ou soltamos.
//
// O Outfit Studio decide entre pan e zoom lendo a tecla FISICA, e nao o
// modificador que vem na mensagem. Isso foi medido: com Shift+meio ele dava
// zoom mesmo com o MK_SHIFT retirado do wParam, e com Ctrl+meio dava pan mesmo
// com o MK_SHIFT posto. Reescrever mensagem nao muda o que GetAsyncKeyState
// responde -- so mexer no teclado de verdade muda.
enum class ShiftOverride {
	None,
	Released, // soltamos um Shift que o usuario segura, para virar pan
	Pressed,  // apertamos um Shift que o usuario nao segura, para virar zoom
};

ShiftOverride g_shiftOverride = ShiftOverride::None;

void SendShift(bool down) {
	INPUT input = {};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = VK_SHIFT;
	input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC));
	input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(input));
}

// Desfaz o que apertamos. O que SOLTAMOS nao e desfeito de proposito.
//
// Se o usuario ainda segura o Shift, o sistema o ve solto ate ele soltar de
// verdade -- inconveniente por alguns segundos e que se corrige sozinho. Se em
// vez disso reapertassemos, e ele ja tivesse soltado no meio do arrasto,
// ficaria um Shift preso no sistema inteiro, que e muito pior.
void ClearShiftOverride() {
	if (g_shiftOverride == ShiftOverride::Pressed)
		SendShift(false);
	g_shiftOverride = ShiftOverride::None;
}

// Reafirma o Shift sintetico durante o arrasto.
//
// Soltar uma vez no inicio nao basta: tecla modificadora SEGURADA gera
// repeticoes, e cada repeticao devolve o estado para "apertada". O sintoma era
// o arrasto comecar em pan e virar zoom no meio do caminho, como se os dois
// comportamentos estivessem ligados ao mesmo tempo.
//
// Custa uma leitura de estado por movimento do mouse, e so mexe no teclado
// quando ele saiu do lugar.
void HoldShiftOverride() {
	const bool down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

	if (g_shiftOverride == ShiftOverride::Released && down)
		SendShift(false);
	else if (g_shiftOverride == ShiftOverride::Pressed && !down)
		SendShift(true);
}

// O interruptor do menu chama isto.
void OnMenuToggle(bool checked) {
	BlenderCamera::SetEnabled(checked);
	LogF("camera blender: %s pelo menu", checked ? "ligada" : "desligada");
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
	ClearShiftOverride();
	g_state = CameraState();

	g_canvas = FindLargestVisibleByClass(frame, L"wxGLCanvas");
	if (!g_canvas) {
		LogF("camera blender: nenhum wxGLCanvas visivel, feature desligada");
		return false;
	}

	// O item de menu e um extra: se nao der para criar, a feature continua
	// funcionando pelo INI.
	HMENU view = MenuToggle::FindMenu(frame, "menuView");
	g_commandId = view ? MenuToggle::Add(frame, view, L"Blender camera keymap",
										 g_enabled, OnMenuToggle)
					   : 0;
	if (g_commandId == 0)
		LogF("camera blender: sem item de menu -- fica so o INI");

	g_installed = true;
	LogF("camera blender: instalada no canvas %p, comeca %s",
		 static_cast<void*>(g_canvas), g_enabled ? "ligada" : "desligada");
	return true;
}

void Uninstall() {
	// Quem tira os itens do menu e o proprio modulo de toggle: ele conhece
	// todos, inclusive os das outras features, e tira o separador junto.
	g_frame = nullptr;
	g_canvas = nullptr;
	g_commandId = 0;
	g_installed = false;
	ClearShiftOverride();
	g_state = CameraState();
}

bool IsEnabled() {
	return g_enabled;
}

void SetEnabled(bool enabled) {
	g_enabled = enabled;
	// Sair no meio de um arrasto deixaria o estado preso em "orbitando", e a
	// partir dai todo movimento do mouse viria com o botao trocado.
	ClearShiftOverride();
	g_state = CameraState();
	MenuToggle::SetChecked(g_commandId, enabled);
}

// Fecha um arrasto que ficou preso, mandando ao programa o "soltar" que ele
// esta esperando. Sem esse fechamento o Outfit Studio continuaria achando que o
// botao direito esta pressionado, mesmo com o nosso estado ja limpo.
void EndStuckDrag() {
	const UINT closing = g_state.orbiting ? WM_RBUTTONUP : WM_MBUTTONUP;
	ClearShiftOverride();
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

	// Rede de seguranca do Shift sintetico: fora de um arrasto ele nao tem
	// razao de existir. Um Shift preso no sistema inteiro seria o pior estrago
	// que este mod poderia causar, entao qualquer mensagem que chegue sem
	// arrasto em curso o desfaz.
	if (!dragging && g_shiftOverride != ShiftOverride::None)
		ClearShiftOverride();

	const bool wasDragging = dragging;

	UINT message = msg->message;
	WPARAM wParam = msg->wParam;
	if (!TranslateCameraMessage(g_state, message, wParam))
		return;

	msg->message = message;
	msg->wParam = wParam;

	// A traducao acabou de decidir o modo do arrasto. Agora o teclado precisa
	// contar a mesma historia que a mensagem, porque e nele que o Outfit
	// Studio olha.
	if (!wasDragging) {
		if (g_state.panning) {
			// Pan e o meio SEM Shift. O usuario segura Shift, entao soltamos.
			g_shiftOverride = ShiftOverride::Released;
			SendShift(false);
			LogF("camera blender: pan -- Shift solto no sistema durante o arrasto");
		} else if (g_state.zooming) {
			// Zoom e o meio COM Shift. O usuario segura Ctrl, entao apertamos.
			g_shiftOverride = ShiftOverride::Pressed;
			SendShift(true);
			LogF("camera blender: zoom -- Shift apertado no sistema durante o arrasto");
		}
	}

	// Durante o arrasto, o teclado tem que continuar contando a mesma historia.
	// A repeticao da tecla segurada desfaz o que soltamos, e sem reafirmar o
	// arrasto comeca em pan e vira zoom no meio.
	if (msg->message == WM_MOUSEMOVE && g_shiftOverride != ShiftOverride::None)
		HoldShiftOverride();

	// Fim do arrasto: o teclado volta a ser do usuario.
	if (wasDragging && !g_state.orbiting && !g_state.panning && !g_state.zooming)
		ClearShiftOverride();
}

} // namespace BlenderCamera
