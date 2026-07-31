#include "features/brush_resize.h"

#include <commctrl.h>

#include <cmath>
#include <cstdlib>
#include <string>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
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

HWND g_statusBar = nullptr;
volatile LONG g_paintCount = 0;
const UINT_PTR kCanvasSubclassId = 0xB507;

// Le o painel 2 da barra de status, onde o OnIncBrush escreve "Rad: %f". E a
// unica forma, de fora, de saber se o comando de brush surtiu efeito -- separa
// "o comando nao funciona" de "o desenho nao atualiza".
std::wstring ReadRadius() {
	if (!g_statusBar || !IsWindow(g_statusBar))
		return std::wstring(L"(sem barra de status)");

	wchar_t text[128] = {};
	SendMessageW(g_statusBar, SB_GETTEXTW, 2, reinterpret_cast<LPARAM>(text));
	return text[0] ? std::wstring(text) : std::wstring(L"(vazio)");
}

LRESULT CALLBACK CanvasSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_PAINT)
		InterlockedIncrement(&g_paintCount);
	return DefSubclassProc(hwnd, msg, wp, lp);
}

// Precisa rodar NA thread dona da janela: SetWindowSubclass falha em silencio
// quando chamado de fora dela, e foi por isso que a contagem de pintura da
// rodada anterior veio zerada e nao pode ser usada como prova.
void InstallCanvasCounter(void*) {
	if (g_canvas && !SetWindowSubclass(g_canvas, CanvasSubclassProc, kCanvasSubclassId, 0))
		LogF("brush_resize: nao consegui subclassar o canvas para contar pintura");
}

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

	// RedrawWindow em vez de InvalidateRect + UpdateWindow: e a versao forte,
	// que ignora janela sem area invalida acumulada e alcanca filhos. O
	// UpdateWindow so entrega WM_PAINT se a regiao de atualizacao nao estiver
	// vazia, e havia duvida sobre isso estar acontecendo.
	RedrawWindow(g_canvas, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
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
	// Pode haver mais de um wxGLCanvas -- a janela de preview tem o seu. O que
	// interessa e a view 3D principal: a maior visivel.
	g_canvas = nullptr;
	long bestArea = 0;
	for (int nth = 0; nth < 8; ++nth) {
		HWND candidate = FindDescendantByClass(frame, L"wxGLCanvas", nth);
		if (!candidate)
			break;

		RECT rc = {};
		GetWindowRect(candidate, &rc);
		const long area = static_cast<long>(rc.right - rc.left) * (rc.bottom - rc.top);
		const bool visible = IsWindowVisible(candidate) != FALSE;
		LogF("brush_resize: canvas #%d hwnd=%p %ldx%ld visivel=%d", nth,
			 static_cast<void*>(candidate), rc.right - rc.left, rc.bottom - rc.top, visible ? 1 : 0);

		if (visible && area > bestArea) {
			bestArea = area;
			g_canvas = candidate;
		}
	}

	if (!g_canvas)
		LogF("brush_resize: nenhum wxGLCanvas visivel -- o circulo nao vai redesenhar");

	RunOnUiThread(frame, InstallCanvasCounter, nullptr);

	g_statusBar = FindDescendantByClass(frame, STATUSCLASSNAMEW);

	LogF("brush_resize: pronto (canvas=%p, statusbar=%p, aumentar=%u diminuir=%u)",
		 static_cast<void*>(g_canvas), static_cast<void*>(g_statusBar), g_increaseId, g_decreaseId);
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

	const int steps = StepsToApply(screenX - g_anchorScreenX, Cfg().brushResizeSensitivity, g_applied);
	if (steps == 0)
		return; // nada a fazer; nao polui o log nem forca repaint atoa

	const LONG paintsBefore = g_paintCount;
	ApplySteps(steps);
	const std::wstring radioDepois = ReadRadius();
	Redraw();

	LogF("brush: %+d passos -> total %+d | status='%ls' | paints %ld->%ld",
		 steps, g_applied, radioDepois.c_str(), paintsBefore, g_paintCount);
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
