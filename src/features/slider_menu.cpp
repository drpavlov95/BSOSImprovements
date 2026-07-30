#include "features/slider_menu.h"

#include "core/log.h"
#include "win32/menu.h"

SliderMenuRefs ResolveSliderMenu(const std::wstring& appDir) {
	SliderMenuRefs refs;

	const std::wstring xrc = appDir + L"res\\xrc\\OutfitStudio.xrc";
	refs.importSubmenu = ResolveMenuPath(xrc.c_str(), "menuImportSlider");
	refs.exportSubmenu = ResolveMenuPath(xrc.c_str(), "menuExportSlider");
	refs.importObjItem = ResolveMenuPath(xrc.c_str(), "sliderImportOBJ");
	refs.exportObjItem = ResolveMenuPath(xrc.c_str(), "sliderExportOBJ");

	refs.ok = !refs.importSubmenu.empty() && !refs.exportSubmenu.empty() &&
			  !refs.importObjItem.empty() && !refs.exportObjItem.empty();

	if (!refs.ok) {
		// Degradar em silencio e melhor que disparar o comando errado: um
		// caminho resolvido pela metade apontaria para outro item de menu.
		LogF("slider_menu: nao resolvi os caminhos em %ls -- atalhos de OBJ desligados", xrc.c_str());
	}
	return refs;
}

bool IsSliderEditModeActive(HWND frame, const SliderMenuRefs& refs) {
	if (!refs.ok || !frame)
		return false;

	HMENU bar = GetMenu(frame);
	if (!bar)
		return false;

	return IsEnabledAtPath(bar, refs.importSubmenu);
}

bool InvokeMenuCommand(HWND frame, const MenuPath& path) {
	if (!frame || path.empty())
		return false;

	HMENU bar = GetMenu(frame);
	if (!bar)
		return false;

	const UINT id = CommandIdAtPath(bar, path);
	if (id == 0)
		return false;

	return PostMessageW(frame, WM_COMMAND, MAKEWPARAM(id, 0), 0) != FALSE;
}
