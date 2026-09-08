#include "features/stroke_stabilizer.h"

#include <cmath>
#include <string>

#include "core/host.h"
#include "core/log.h"
#include "win32/menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

HWND g_frame = nullptr;
HWND g_canvas = nullptr;
bool g_installed = false;

// Caminho do item "Select" no menu de ferramentas. E por ele que se sabe se o
// que esta na mao e um pincel: os itens de "Current Tool" sao de radio, entao o
// menu vivo carrega a marca no que estiver em uso.
MenuTrail g_selectTool;

bool g_stroking = false;
POINT g_brush = {};

// A ferramenta de selecao nao pinta: com ela o botao esquerdo mexe a camera.
// Estabilizar isso atrasaria a navegacao, que e defeito e nao ajuda.
bool SelectToolActive() {
	if (g_selectTool.empty() || !g_frame)
		return false;

	HMENU bar = GetMenu(g_frame);
	if (!bar)
		return false;

	return IsCheckedAtLabeledPath(bar, g_selectTool.path, g_selectTool.labels);
}

POINT ClientPointOf(const MSG* msg) {
	POINT point;
	point.x = static_cast<short>(LOWORD(msg->lParam));
	point.y = static_cast<short>(HIWORD(msg->lParam));
	return point;
}

// Escreve a posicao do pincel na mensagem, nas duas formas que ela carrega: a
// de cliente, que e a que o programa le, e a de tela, que precisa acompanhar
// para nao virar uma mensagem incoerente consigo mesma.
void WritePoint(MSG* msg, POINT client) {
	const POINT before = ClientPointOf(msg);

	msg->lParam = MAKELPARAM(static_cast<WORD>(client.x), static_cast<WORD>(client.y));
	msg->pt.x += client.x - before.x;
	msg->pt.y += client.y - before.y;
}

bool LeftButtonDown() {
	return (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
}

} // namespace

POINT StabilizeStroke(POINT brush, POINT cursor, int radius) {
	if (radius <= 0)
		return cursor;

	const double dx = static_cast<double>(cursor.x) - brush.x;
	const double dy = static_cast<double>(cursor.y) - brush.y;
	const double distance = std::sqrt(dx * dx + dy * dy);

	// Dentro da corda o pincel nao anda. E este ramo que mata o tremor: tremor
	// e movimento pequeno, e movimento pequeno nao chega a esticar a corda.
	if (distance <= static_cast<double>(radius))
		return brush;

	// Esticou: o pincel e arrastado ate ficar exatamente a um raio do cursor.
	const double scale = (distance - radius) / distance;

	POINT moved;
	moved.x = brush.x + static_cast<LONG>(std::lround(dx * scale));
	moved.y = brush.y + static_cast<LONG>(std::lround(dy * scale));
	return moved;
}

namespace StrokeStabilizer {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	if (Cfg().stabilizerRadius <= 0) {
		LogF("estabilizador: raio zero no INI, desligado");
		return false;
	}

	g_canvas = FindLargestVisibleByClass(frame, L"wxGLCanvas");
	if (!g_canvas) {
		LogF("estabilizador: nenhum wxGLCanvas visivel, desligado");
		return false;
	}

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	g_selectTool = ResolveMenuTrail(xrc.c_str(), "btnSelect");
	if (g_selectTool.empty()) {
		// Sem esse caminho nao da para distinguir pincel de selecao, e
		// estabilizar a camera seria pior que nao estabilizar nada.
		LogF("estabilizador: nao resolvi btnSelect no XRC, desligado");
		return false;
	}

	g_installed = true;
	LogF("estabilizador: ligado no canvas %p, raio %d px",
		 static_cast<void*>(g_canvas), Cfg().stabilizerRadius);
	return true;
}

void Uninstall() {
	g_frame = nullptr;
	g_canvas = nullptr;
	g_installed = false;
	g_stroking = false;
	g_selectTool = MenuTrail();
}

bool IsEnabled() {
	return g_installed;
}

void RewriteStrokeMessage(MSG* msg) {
	if (!g_installed || !msg || msg->hwnd != g_canvas)
		return;

	switch (msg->message) {
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			// A decisao de estabilizar e tomada no aperto e vale o traco
			// inteiro: trocar de ferramenta no meio de um traco nao acontece,
			// e reconsultar o menu a cada movimento custaria uma leitura de
			// menu por pixel.
			if (SelectToolActive()) {
				g_stroking = false;
				return;
			}
			g_stroking = true;
			// O traco comeca exatamente onde o clique caiu. Comecar atrasado
			// deixaria o primeiro pedaco da pincelada fora do lugar.
			g_brush = ClientPointOf(msg);
			return;

		case WM_MOUSEMOVE: {
			if (!g_stroking)
				return;

			// Soltar o botao fora da janela manda o WM_LBUTTONUP para outro
			// processo. Sem isto o traco continuaria "aberto" e todo movimento
			// depois dele sairia atrasado, inclusive fora de qualquer traco.
			if (!LeftButtonDown()) {
				g_stroking = false;
				return;
			}

			g_brush = StabilizeStroke(g_brush, ClientPointOf(msg), Cfg().stabilizerRadius);
			WritePoint(msg, g_brush);
			return;
		}

		case WM_LBUTTONUP:
			if (!g_stroking)
				return;
			// Termina onde o pincel esta, e nao onde o cursor parou: a corda
			// ainda estava esticada, e saltar para o cursor no ultimo instante
			// deixaria um risco reto no fim de cada pincelada.
			WritePoint(msg, g_brush);
			g_stroking = false;
			return;

		default:
			return;
	}
}

} // namespace StrokeStabilizer
