#include "features/brush_resize.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "core/host.h"
#include "core/log.h"
#include "core/theme.h"
#include "core/ui_thread.h"
#include "features/slider_menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

// O range do tamanho sao 300 passos de 0.010 (LimitBrushSize em
// OutfitStudio.h). Passar disso so gera comandos que o Outfit Studio ignora, e
// faria o cancelar devolver o brush para longe do tamanho original.
constexpr int kMaxSizeSteps = 300;

bool g_active = false;
BrushResize::Target g_target = BrushResize::Target::Size;
int g_anchorScreenX = 0;
int g_anchorScreenY = 0;
int g_applied = 0;
int g_jitter = 0;
bool g_cuePosted = false;

HWND g_frame = nullptr;
HWND g_canvas = nullptr; // o wxGLCanvas, fixado na instalacao
POINT g_anchorClient = {}; // a ancora em coordenadas do canvas
HWND g_cueWindow = nullptr; // overlay transparente do indicador de forca
bool g_cueClassRegistered = false;
bool g_cueVisible = false;
bool g_cueUpdateErrorLogged = false;
int g_lastCuePercent = -1;
bool g_cueDark = false;

// Ids lidos uma unica vez na instalacao. Reler o menu a cada passo, alem de
// caro -- um arrasto de ponta a ponta sao 300 passos -- se mostrou pouco
// confiavel: a leitura funcionava na inicializacao e devolvia zero depois.
UINT g_increaseId = 0;
UINT g_decreaseId = 0;
UINT g_increaseStrId = 0;
UINT g_decreaseStrId = 0;

bool IsStrength() {
	return g_target == BrushResize::Target::Strength;
}

UINT IncreaseId() {
	return IsStrength() ? g_increaseStrId : g_increaseId;
}

UINT DecreaseId() {
	return IsStrength() ? g_decreaseStrId : g_decreaseId;
}

// Quantos passos a grandeza em curso tem.
//
// O do tamanho vem do proprio Outfit Studio. O da forca nao esta escrito em
// recurso nenhum -- o popup do Space e construido em codigo -- entao ele e
// configuravel, e o log registra o valor lido da barra de status no inicio e no
// fim de cada arrasto, que e como se descobre o numero certo sem adivinhar.
int MaxSteps() {
	return IsStrength() ? Cfg().brushStrengthSteps : kMaxSizeSteps;
}

const char* TargetName() {
	return IsStrength() ? "forca" : "tamanho";
}

HWND g_statusBar = nullptr;
volatile LONG g_paintCount = 0;
const UINT_PTR kCanvasSubclassId = 0xB507;
constexpr UINT kDrawCueMessage = WM_APP + 0x507;
constexpr wchar_t kCueClassName[] = L"BSOSImprovements_StrengthCue";
constexpr int kCueWidth = 188;
constexpr int kCueHeight = 38;
constexpr int kCueGapAboveAnchor = 72;

bool ReadStrengthPercent(int& percent) {
	if (!g_statusBar || !IsWindow(g_statusBar))
		return false;
	const int panels = static_cast<int>(SendMessageW(g_statusBar, SB_GETPARTS, 0, 0));
	for (int i = 0; i < panels && i < 8; ++i) {
		wchar_t text[128] = {};
		SendMessageW(g_statusBar, SB_GETTEXTW, static_cast<WPARAM>(i),
					 reinterpret_cast<LPARAM>(text));
		if (wcsncmp(text, L"Str:", 4) != 0)
			continue;
		wchar_t* end = nullptr;
		const double value = wcstod(text + 4, &end);
		if (end == text + 4 || !std::isfinite(value))
			return false;
		percent = std::clamp(static_cast<int>(std::lround(value * 100.0)), 0, 100);
		return true;
	}
	return false;
}

int CuePercent() {
	int percent = 0;
	if (ReadStrengthPercent(percent))
		return percent;
	const int maxSteps = MaxSteps();
	if (maxSteps <= 0)
		return 0;
	const double fallback = 50.0 + 50.0 * static_cast<double>(g_applied) / maxSteps;
	return std::clamp(static_cast<int>(std::lround(fallback)), 0, 100);
}

void PaintStrengthBar(HDC dc, int percent) {
	const COLORREF keyColor = RGB(255, 0, 255);
	RECT whole = {0, 0, kCueWidth, kCueHeight};
	HBRUSH key = CreateSolidBrush(keyColor);
	FillRect(dc, &whole, key);
	DeleteObject(key);

	// Aparencia deliberadamente nativa: as mesmas cores e a mesma fonte que
	// os controles Win32/wxWidgets do Outfit Studio recebem do Windows.
	RECT panel = {0, 0, kCueWidth, kCueHeight};
	HBRUSH panelBrush = g_cueDark ? EditBackgroundBrush() : GetSysColorBrush(COLOR_BTNFACE);
	FillRect(dc, &panel, panelBrush);
	HBRUSH borderBrush = g_cueDark ? DarkBackgroundBrush() : GetSysColorBrush(COLOR_3DSHADOW);
	FrameRect(dc, &panel, borderBrush);

	HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, g_cueDark ? kDarkText : GetSysColor(COLOR_BTNTEXT));
	wchar_t label[32] = {};
	swprintf_s(label, L"Strength: %d%%", percent);
	RECT textRect = {7, 2, kCueWidth - 7, 19};
	DrawTextW(dc, label, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
	SelectObject(dc, oldFont);

	RECT track = {7, 23, kCueWidth - 7, 31};
	FillRect(dc, &track, borderBrush);
	InflateRect(&track, -1, -1);
	FillRect(dc, &track, g_cueDark ? DarkBackgroundBrush() : GetSysColorBrush(COLOR_WINDOW));
	RECT fill = track;
	fill.right = fill.left + (fill.right - fill.left) * percent / 100;
	if (fill.right > fill.left) {
		HBRUSH red = CreateSolidBrush(RGB(220, 45, 45));
		FillRect(dc, &fill, red);
		DeleteObject(red);
	}
}

LRESULT CALLBACK CueWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCHITTEST)
		return HTTRANSPARENT;
	if (msg == WM_MOUSEACTIVATE)
		return MA_NOACTIVATE;
	if (msg == WM_ERASEBKGND)
		return 1;
	if (msg == WM_PAINT) {
		PAINTSTRUCT ps = {};
		BeginPaint(hwnd, &ps);
		EndPaint(hwnd, &ps);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

bool EnsureCueWindow() {
	if (!g_frame || !IsWindow(g_frame) || !g_canvas || !IsWindow(g_canvas))
		return false;
	if (!g_cueClassRegistered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = CueWindowProc;
		wc.hInstance = SelfModule();
		wc.lpszClassName = kCueClassName;
		if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return false;
		g_cueClassRegistered = true;
	}
	if (g_cueWindow && IsWindow(g_cueWindow))
		return true;
	g_cueWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
			WS_EX_TOOLWINDOW,
			kCueClassName, L"", WS_POPUP, 0, 0, kCueWidth, kCueHeight,
			g_frame, nullptr, SelfModule(), nullptr);
	if (!g_cueWindow)
		return false;
	ShowWindow(g_cueWindow, SW_HIDE);
	LogF("brush forca: barra visual criada hwnd=%p", static_cast<void*>(g_cueWindow));
	return true;
}

void UpdateStrengthCue() {
	const bool visible = g_active && IsStrength();
	if (!visible) {
		if (g_cueWindow && IsWindow(g_cueWindow) && g_cueVisible)
			ShowWindow(g_cueWindow, SW_HIDE);
		g_cueVisible = false;
		g_lastCuePercent = -1;
		return;
	}
	if (!EnsureCueWindow())
		return;
	const int percent = CuePercent();
	if (g_cueVisible && percent == g_lastCuePercent)
		return;
	g_lastCuePercent = percent;

	RECT canvas = {};
	GetWindowRect(g_canvas, &canvas);
	int x = g_anchorScreenX - kCueWidth / 2;
	int y = g_anchorScreenY - kCueHeight - kCueGapAboveAnchor;
	const int maxX = (std::max)(canvas.left, canvas.right - kCueWidth);
	const int maxY = (std::max)(canvas.top, canvas.bottom - kCueHeight);
	x = (std::clamp)(x, static_cast<int>(canvas.left), maxX);
	y = (std::clamp)(y, static_cast<int>(canvas.top), maxY);

	HDC screen = GetDC(nullptr);
	HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
	HBITMAP bitmap = screen ? CreateCompatibleBitmap(screen, kCueWidth, kCueHeight) : nullptr;
	if (!screen || !memory || !bitmap) {
		if (bitmap) DeleteObject(bitmap);
		if (memory) DeleteDC(memory);
		if (screen) ReleaseDC(nullptr, screen);
		return;
	}
	HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
	PaintStrengthBar(memory, percent);
	POINT destination = {x, y};
	POINT source = {0, 0};
	SIZE size = {kCueWidth, kCueHeight};
	const BOOL updated = UpdateLayeredWindow(g_cueWindow, screen, &destination, &size,
			memory, &source, RGB(255, 0, 255), nullptr, ULW_COLORKEY);
	const DWORD updateError = updated ? ERROR_SUCCESS : GetLastError();
	SelectObject(memory, oldBitmap);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(nullptr, screen);
	if (!updated) {
		if (!g_cueUpdateErrorLogged) {
			LogF("brush forca: UpdateLayeredWindow falhou (%lu)", updateError);
			g_cueUpdateErrorLogged = true;
		}
		return;
	}
	if (!g_cueVisible) {
		ShowWindow(g_cueWindow, SW_SHOWNOACTIVATE);
		g_cueVisible = true;
	}
}

void DrawStrengthCue(HWND) {
	UpdateStrengthCue();
}

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

// Despeja todos os paineis da barra de status.
//
// Serve para descobrir ONDE o Outfit Studio escreve a forca. O painel 2 e o do
// raio, e isso se sabe; o da forca nao esta documentado em lugar nenhum que de
// para ler. Uma linha por arrasto de forca no log resolve a duvida sem
// adivinhacao, e e o que permite acertar BrushStrengthSteps.
void LogAllStatusPanels() {
	if (!g_statusBar || !IsWindow(g_statusBar)) {
		LogF("brush forca: sem barra de status para ler o valor");
		return;
	}

	const int panels = static_cast<int>(SendMessageW(g_statusBar, SB_GETPARTS, 0, 0));
	for (int i = 0; i < panels && i < 8; ++i) {
		wchar_t text[128] = {};
		SendMessageW(g_statusBar, SB_GETTEXTW, static_cast<WPARAM>(i),
					 reinterpret_cast<LPARAM>(text));
		LogF("brush forca: painel %d = '%ls'", i, text);
	}
}

void ExitBecauseCaptureLost();

LRESULT CALLBACK CanvasSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == kDrawCueMessage) {
		g_cuePosted = false;
		DrawStrengthCue(hwnd);
		return 0;
	}

	if (msg == WM_PAINT)
		InterlockedIncrement(&g_paintCount);

	// Perder a captura sem passar por Confirm/Cancel deixaria o modo ligado
	// para sempre, e a partir dai todo movimento do mouse seria reescrito para
	// a ancora -- o brush congelaria de vez. WM_CAPTURECHANGED e enviada, nao
	// postada, entao o hook de mensagens nao a enxerga; so a subclasse.
	if (msg == WM_CAPTURECHANGED)
		ExitBecauseCaptureLost();

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

	const UINT id = (steps > 0) ? IncreaseId() : DecreaseId();
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
// Sai do modo mantendo o tamanho ja aplicado. Nao restaura, porque perder a
// captura nao e o mesmo que o usuario cancelar.
void ExitBecauseCaptureLost() {
	if (!g_active)
		return;
	g_active = false;
	LogF("brush %s: captura perdida, modo encerrado (%+d passos mantidos)", TargetName(), g_applied);
	g_applied = 0;
	g_cuePosted = false;
	UpdateStrengthCue();
	if (g_canvas && IsWindow(g_canvas))
		RedrawWindow(g_canvas, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

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

int StepsToApply(int deltaPixels, float sensitivity, int alreadyApplied, int maxSteps) {
	if (sensitivity <= 0.0f || maxSteps <= 0)
		return 0;

	long target = std::lround(static_cast<double>(deltaPixels) * sensitivity);
	if (target > maxSteps)
		target = maxSteps;
	else if (target < -maxSteps)
		target = -maxSteps;

	return static_cast<int>(target) - alreadyApplied;
}

namespace BrushResize {

bool Install(HWND frame) {
	g_frame = frame;
	g_active = false;
	g_applied = 0;

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	const MenuTrail increasePath = ResolveMenuTrail(xrc.c_str(), "btnIncreaseSize");
	const MenuTrail decreasePath = ResolveMenuTrail(xrc.c_str(), "btnDecreaseSize");

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

	// A forca e opcional: se ela nao resolver, o tamanho continua funcionando e
	// so a tecla dela deixa de ser ligada.
	g_increaseStrId = MenuCommandId(frame, ResolveMenuTrail(xrc.c_str(), "btnIncreaseStr"));
	g_decreaseStrId = MenuCommandId(frame, ResolveMenuTrail(xrc.c_str(), "btnDecreaseStr"));
	if (g_increaseStrId == 0 || g_decreaseStrId == 0)
		LogF("brush_resize: sem os comandos de forca (aumentar=%u diminuir=%u), so o tamanho",
			 g_increaseStrId, g_decreaseStrId);

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
	g_cueDark = DetectAppearance(AppDir()) == Appearance::Dark;

	LogF("brush_resize: pronto (canvas=%p, statusbar=%p, aumentar=%u diminuir=%u)",
		 static_cast<void*>(g_canvas), static_cast<void*>(g_statusBar), g_increaseId, g_decreaseId);
	return true;
}

void Uninstall() {
	if (g_active && GetCapture() == g_canvas)
		ReleaseCapture();
	if (g_canvas && IsWindow(g_canvas))
		RemoveWindowSubclass(g_canvas, CanvasSubclassProc, kCanvasSubclassId);
	if (g_cueWindow && IsWindow(g_cueWindow))
		DestroyWindow(g_cueWindow);
	g_cueWindow = nullptr;
	g_cueVisible = false;
	g_cueUpdateErrorLogged = false;
	g_lastCuePercent = -1;
	g_active = false;
	g_applied = 0;
	g_cuePosted = false;
	g_frame = nullptr;
	g_canvas = nullptr;
	g_increaseId = 0;
	g_decreaseId = 0;
	g_increaseStrId = 0;
	g_decreaseStrId = 0;
}

bool IsActive() {
	return g_active;
}

bool Supports(Target target) {
	if (target == Target::Strength)
		return g_increaseStrId != 0 && g_decreaseStrId != 0;
	return g_increaseId != 0 && g_decreaseId != 0;
}

void Begin(int anchorScreenX, int anchorScreenY, Target target) {
	g_active = true;
	g_target = target;
	g_anchorScreenX = anchorScreenX;
	g_anchorScreenY = anchorScreenY;
	g_applied = 0;
	g_cuePosted = false;
	g_cueUpdateErrorLogged = false;
	g_lastCuePercent = -1;

	// Converte a ancora uma unica vez, enquanto o ponteiro ainda esta onde a
	// tecla foi apertada.
	g_anchorClient.x = anchorScreenX;
	g_anchorClient.y = anchorScreenY;
	if (g_canvas && IsWindow(g_canvas)) {
		ScreenToClient(g_canvas, &g_anchorClient);
		// Captura o mouse para as mensagens continuarem chegando no canvas
		// mesmo quando o ponteiro sai dele -- e ele sai, num arrasto de
		// algumas centenas de pixels.
		SetCapture(g_canvas);
	}

	g_paintCount = 0;
	LogF("brush %s: modo ligado (ancora tela %d,%d -> canvas %ld,%ld, %ls)", TargetName(),
		 anchorScreenX, anchorScreenY, g_anchorClient.x, g_anchorClient.y, ReadRadius().c_str());
	if (IsStrength())
		LogAllStatusPanels();
	if (IsStrength()) {
		Redraw();
		UpdateStrengthCue();
	}
}

void RewriteMouseMove(MSG* msg) {
	if (!g_active || !msg)
		return;

	const int steps = StepsToApply(msg->pt.x - g_anchorScreenX, Cfg().brushResizeSensitivity, g_applied, MaxSteps());
	if (steps != 0)
		ApplySteps(steps);

	// Reescreve para a ancora e deixa a mensagem seguir. O app trata o
	// movimento pelo caminho de sempre, mas com o cursor parado, entao o
	// circulo muda de tamanho sem andar.
	//
	// O deslocamento de 1 pixel alternado existe porque mandar sempre a
	// coordenada identica fez o app parar de redesenhar. Um pixel na tela e
	// imperceptivel no circulo, mas garante que cada mensagem seja um
	// movimento de verdade.
	g_jitter = 1 - g_jitter;
	const LONG x = g_anchorClient.x + g_jitter;

	msg->hwnd = g_canvas ? g_canvas : msg->hwnd;
	msg->lParam = MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(g_anchorClient.y));
	msg->pt.x = g_anchorScreenX + g_jitter;
	msg->pt.y = g_anchorScreenY;

	// Cinto e suspensorio: forca a pintura tambem por conta propria, para nao
	// depender so do caminho interno do app.
	if (g_canvas && IsWindow(g_canvas)) {
		RedrawWindow(g_canvas, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
		// O app ainda vai processar o WM_MOUSEMOVE depois que o hook retornar e
		// pode trocar o backbuffer outra vez. Um redraw postado fica na fila para
		// depois desse processamento, garantindo que o cue seja o ultimo desenho.
		if (IsStrength() && !g_cuePosted) {
			g_cuePosted = PostMessageW(g_canvas, kDrawCueMessage, 0, 0) != FALSE;
		}
	}
}

void Confirm() {
	if (!g_active)
		return;
	g_active = false;
	if (GetCapture() == g_canvas)
		ReleaseCapture();
	UpdateStrengthCue();
	// Um resumo por arrasto, nao um por movimento: cada LogF abre e fecha o
	// arquivo, e um arrasto gera centenas de mensagens.
	LogF("brush %s: confirmado (%+d passos, %ls, %ld repaints)", TargetName(), g_applied,
		 ReadRadius().c_str(), g_paintCount);
	g_applied = 0;
	g_cuePosted = false;
	Redraw();
}

void Cancel() {
	if (!g_active)
		return;
	ApplySteps(-g_applied);
	g_active = false;
	if (GetCapture() == g_canvas)
		ReleaseCapture();
	UpdateStrengthCue();
	LogF("brush %s: cancelado, valor restaurado", TargetName());
	g_applied = 0;
	g_cuePosted = false;
	Redraw();
}

} // namespace BrushResize
