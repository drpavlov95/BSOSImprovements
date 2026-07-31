#include "features/brush_resize.h"

#include <cmath>
#include <cstdlib>

#include "core/host.h"
#include "core/log.h"
#include "features/slider_menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

// O range do brush sao 300 passos de 0.010 (LimitBrushSize em OutfitStudio.h).
// Passar disso so gera comandos que o Outfit Studio ignora, e faria o cancelar
// devolver o brush para longe do tamanho original.
constexpr int kMaxSteps = 300;

bool g_active = false;
int g_anchorScreenX = 0;
int g_applied = 0;

HWND g_frame = nullptr;
HWND g_canvas = nullptr; // o wxGLCanvas, fixado na instalacao
POINT g_anchorClient = {}; // a ancora em coordenadas do canvas

// Ids lidos uma unica vez na instalacao. Reler o menu a cada passo, alem de
// caro -- um arrasto de ponta a ponta sao 300 passos -- se mostrou pouco
// confiavel: a leitura funcionava na inicializacao e devolvia zero depois.
UINT g_increaseId = 0;
UINT g_decreaseId = 0;

void ApplySteps(int steps) {
	if (steps == 0)
		return;

	const UINT id = (steps > 0) ? g_increaseId : g_decreaseId;
	if (id == 0)
		return;

	// Sincrono de proposito. Com PostMessage os comandos ficariam na fila e o
	// redesenho logo abaixo mostraria o tamanho antigo -- o circulo so mudaria
	// quando alguma outra coisa provocasse um repaint, tipico do clique de
	// confirmacao. Aqui o tamanho ja mudou quando Redraw roda.
	const int count = std::abs(steps);
	for (int i = 0; i < count; ++i)
		SendMenuCommandId(g_frame, id);

	g_applied += steps;
}

// Redesenha o circulo no tamanho novo, na posicao congelada.
//
// Precisa dos dois passos. O WM_MOUSEMOVE faz o Outfit Studio recalcular o
// cursor: GLSurface::UpdateCursor termina em ShowCursor(collided), entao sem
// uma posicao que acerte o mesh o circulo simplesmente some. E o
// InvalidateRect forca o EVT_PAINT, que chama RenderOneFrame -- o branch de
// mouse solto do OnMouseMove atualiza o cursor mas nunca redesenha, e o
// OnIncBrush, sendo comando de menu, nao faz nem um nem outro.
//
// A posicao vai sempre em coordenadas do canvas guardadas no inicio, nunca
// derivadas da janela sob o mouse: durante o arrasto o ponteiro sai do canvas,
// e converter contra a janela errada mandaria o cursor para fora do mesh.
void Redraw() {
	if (!g_canvas || !IsWindow(g_canvas))
		return;

	SendMessageW(g_canvas, WM_MOUSEMOVE, 0,
				 MAKELPARAM(static_cast<WORD>(g_anchorClient.x), static_cast<WORD>(g_anchorClient.y)));
	InvalidateRect(g_canvas, nullptr, FALSE);
	UpdateWindow(g_canvas);
}

} // namespace

int StepsToApply(int deltaPixels, float sensitivity, int alreadyApplied) {
	if (sensitivity <= 0.0f)
		return 0;

	long target = std::lround(static_cast<double>(deltaPixels) * sensitivity);
	if (target > kMaxSteps)
		target = kMaxSteps;
	else if (target < -kMaxSteps)
		target = -kMaxSteps;

	return static_cast<int>(target) - alreadyApplied;
}

namespace BrushResize {

bool Install(HWND frame) {
	g_frame = frame;
	g_active = false;
	g_applied = 0;

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	const MenuPath increasePath = ResolveMenuPath(xrc.c_str(), "btnIncreaseSize");
	const MenuPath decreasePath = ResolveMenuPath(xrc.c_str(), "btnDecreaseSize");

	if (increasePath.empty() || decreasePath.empty()) {
		LogF("brush_resize: nao resolvi btnIncreaseSize/btnDecreaseSize no XRC");
		return false;
	}

	g_increaseId = MenuCommandId(frame, increasePath);
	g_decreaseId = MenuCommandId(frame, decreasePath);
	if (g_increaseId == 0 || g_decreaseId == 0) {
		LogF("brush_resize: nao consegui ler os ids (aumentar=%u diminuir=%u)", g_increaseId, g_decreaseId);
		return false;
	}

	// A view 3D e um wxGLCanvas. Fixar o handle aqui evita depender de qual
	// janela esta sob o mouse durante o arrasto.
	g_canvas = FindDescendantByClass(frame, L"wxGLCanvas");
	if (!g_canvas)
		LogF("brush_resize: wxGLCanvas nao encontrado -- o circulo nao vai redesenhar");

	LogF("brush_resize: pronto (canvas=%p, aumentar=%u diminuir=%u)",
		 static_cast<void*>(g_canvas), g_increaseId, g_decreaseId);
	return true;
}

void Uninstall() {
	g_active = false;
	g_applied = 0;
	g_frame = nullptr;
	g_canvas = nullptr;
	g_increaseId = 0;
	g_decreaseId = 0;
}

bool IsActive() {
	return g_active;
}

void Begin(int anchorScreenX, int anchorScreenY) {
	g_active = true;
	g_anchorScreenX = anchorScreenX;
	g_applied = 0;

	// Converte a ancora uma unica vez, enquanto o ponteiro ainda esta onde a
	// tecla foi apertada.
	g_anchorClient.x = anchorScreenX;
	g_anchorClient.y = anchorScreenY;
	if (g_canvas && IsWindow(g_canvas))
		ScreenToClient(g_canvas, &g_anchorClient);

	LogF("brush resize: modo ligado (ancora tela %d,%d -> canvas %ld,%ld)",
		 anchorScreenX, anchorScreenY, g_anchorClient.x, g_anchorClient.y);
	Redraw();
}

void OnMouseMove(int screenX) {
	if (!g_active)
		return;
	ApplySteps(StepsToApply(screenX - g_anchorScreenX, Cfg().brushResizeSensitivity, g_applied));
	Redraw();
}

void Confirm() {
	if (!g_active)
		return;
	g_active = false;
	LogF("brush resize: confirmado (%+d passos)", g_applied);
	g_applied = 0;
	Redraw();
}

void Cancel() {
	if (!g_active)
		return;
	ApplySteps(-g_applied);
	g_active = false;
	LogF("brush resize: cancelado, tamanho restaurado");
	g_applied = 0;
	Redraw();
}

} // namespace BrushResize
