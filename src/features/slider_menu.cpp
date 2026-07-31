#include "features/slider_menu.h"

#include <string>

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

UINT MenuCommandId(HWND frame, const MenuPath& path) {
	if (!frame || path.empty())
		return 0;
	HMENU bar = GetMenu(frame);
	return bar ? CommandIdAtPath(bar, path) : 0;
}

bool InvokeMenuCommandId(HWND frame, UINT id) {
	if (!frame || id == 0)
		return false;
	return PostMessageW(frame, WM_COMMAND, MAKEWPARAM(id, 0), 0) != FALSE;
}

bool BindSliderMenu(HWND frame, SliderMenuRefs& refs) {
	if (!refs.ok)
		return false;

	refs.importObjId = MenuCommandId(frame, refs.importObjItem);
	refs.exportObjId = MenuCommandId(frame, refs.exportObjItem);

	if (refs.importObjId == 0 || refs.exportObjId == 0) {
		LogF("slider_menu: nao consegui ler os ids na instalacao (import=%u export=%u)",
			 refs.importObjId, refs.exportObjId);
		refs.ok = false;
		return false;
	}

	LogF("slider_menu: ids guardados -- import=%u export=%u", refs.importObjId, refs.exportObjId);
	return true;
}

namespace {

std::string PathToText(const MenuPath& path) {
	std::string text;
	for (size_t i = 0; i < path.size(); ++i) {
		if (i)
			text += ">";
		text += std::to_string(path[i]);
	}
	return text.empty() ? "(vazio)" : text;
}

} // namespace

void LogSliderMenuState(HWND frame, const SliderMenuRefs& refs) {
	HMENU bar = frame ? GetMenu(frame) : nullptr;
	if (!bar) {
		LogF("slider_menu: o frame nao tem menubar");
		return;
	}

	LogF("slider_menu: import submenu=%s estado=0x%08X | item=%s id=%u",
		 PathToText(refs.importSubmenu).c_str(), StateAtPath(bar, refs.importSubmenu),
		 PathToText(refs.importObjItem).c_str(), CommandIdAtPath(bar, refs.importObjItem));
	LogF("slider_menu: export submenu=%s estado=0x%08X | item=%s id=%u",
		 PathToText(refs.exportSubmenu).c_str(), StateAtPath(bar, refs.exportSubmenu),
		 PathToText(refs.exportObjItem).c_str(), CommandIdAtPath(bar, refs.exportObjItem));
	LogF("slider_menu: MFS_GRAYED=0x%08X, entao estado com esse bit = fora do edit mode",
		 static_cast<unsigned>(MFS_GRAYED));
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
