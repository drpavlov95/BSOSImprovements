#include "features/brush_resize.h"

#include <cmath>
#include <cstdlib>

#include "core/host.h"
#include "core/log.h"
#include "features/slider_menu.h"
#include "xrcmap.h"

namespace {

// O range do brush sao 300 passos de 0.010 (LimitBrushSize em OutfitStudio.h).
// Passar disso so gera comandos que o Outfit Studio ignora, e faria o cancelar
// devolver o brush para longe do tamanho original.
constexpr int kMaxSteps = 300;

bool g_active = false;
int g_anchorX = 0;
int g_applied = 0;
HWND g_frame = nullptr;
MenuPath g_increase;
MenuPath g_decrease;

void ApplySteps(int steps) {
	if (steps == 0)
		return;

	const MenuPath& path = (steps > 0) ? g_increase : g_decrease;
	const int count = std::abs(steps);
	for (int i = 0; i < count; ++i)
		InvokeMenuCommand(g_frame, path);

	g_applied += steps;
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
	g_increase = ResolveMenuPath(xrc.c_str(), "btnIncreaseSize");
	g_decrease = ResolveMenuPath(xrc.c_str(), "btnDecreaseSize");

	if (g_increase.empty() || g_decrease.empty()) {
		LogF("brush_resize: nao resolvi btnIncreaseSize/btnDecreaseSize no XRC");
		return false;
	}
	return true;
}

void Uninstall() {
	g_active = false;
	g_applied = 0;
	g_frame = nullptr;
	g_increase.clear();
	g_decrease.clear();
}

bool IsActive() {
	return g_active;
}

void Begin(int anchorScreenX) {
	g_active = true;
	g_anchorX = anchorScreenX;
	g_applied = 0;
	LogF("brush resize: entrou no modo (ancora x=%d)", anchorScreenX);
}

void OnMouseMove(int screenX) {
	if (!g_active)
		return;
	ApplySteps(StepsToApply(screenX - g_anchorX, Cfg().brushResizeSensitivity, g_applied));
}

void Confirm() {
	if (!g_active)
		return;
	g_active = false;
	LogF("brush resize: confirmado (%+d passos)", g_applied);
	g_applied = 0;
}

void Cancel() {
	if (!g_active)
		return;
	ApplySteps(-g_applied);
	g_active = false;
	LogF("brush resize: cancelado, tamanho restaurado");
	g_applied = 0;
}

} // namespace BrushResize
