#include "features/slider_menu.h"

#include <string>

#include "core/log.h"
#include "win32/menu.h"

SliderMenuRefs ResolveSliderMenu(const std::wstring& appDir) {
	SliderMenuRefs refs;

	const std::wstring xrc = appDir + L"res\\xrc\\OutfitStudio.xrc";
	refs.importSubmenu = ResolveMenuTrail(xrc.c_str(), "menuImportSlider");
	refs.exportSubmenu = ResolveMenuTrail(xrc.c_str(), "menuExportSlider");
	refs.importObjItem = ResolveMenuTrail(xrc.c_str(), "sliderImportOBJ");
	refs.exportObjItem = ResolveMenuTrail(xrc.c_str(), "sliderExportOBJ");

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

	return IsEnabledAtLabeledPath(bar, refs.importSubmenu.path, refs.importSubmenu.labels);
}

UINT MenuCommandId(HWND frame, const MenuTrail& trail) {
	if (!frame || trail.empty())
		return 0;
	HMENU bar = GetMenu(frame);
	return bar ? CommandIdAtLabeledPath(bar, trail.path, trail.labels) : 0;
}

bool InvokeMenuCommandId(HWND frame, UINT id) {
	if (!frame || id == 0)
		return false;
	return PostMessageW(frame, WM_COMMAND, MAKEWPARAM(id, 0), 0) != FALSE;
}

bool SendMenuCommandId(HWND frame, UINT id) {
	if (!frame || id == 0)
		return false;
	SendMessageW(frame, WM_COMMAND, MAKEWPARAM(id, 0), 0);
	return true;
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

// Onde o item foi parar de verdade no menu vivo, que pode nao ser onde o XRC
// diz -- e a diferenca entre os dois e justamente o sintoma de outro mod ter
// inserido item antes dele.
int LiveLeafIndex(HMENU bar, const MenuTrail& trail) {
	int index = -1;
	return ContainerAtLabeledPath(bar, trail.path, trail.labels, index) ? index : -1;
}

} // namespace

void LogSliderMenuState(HWND frame, const SliderMenuRefs& refs) {
	HMENU bar = frame ? GetMenu(frame) : nullptr;
	if (!bar) {
		LogF("slider_menu: o frame nao tem menubar");
		return;
	}

	LogF("slider_menu: import submenu xrc=%s vivo=%d | item xrc=%s vivo=%d id=%u",
		 PathToText(refs.importSubmenu.path).c_str(), LiveLeafIndex(bar, refs.importSubmenu),
		 PathToText(refs.importObjItem.path).c_str(), LiveLeafIndex(bar, refs.importObjItem),
		 CommandIdAtLabeledPath(bar, refs.importObjItem.path, refs.importObjItem.labels));
	LogF("slider_menu: export submenu xrc=%s vivo=%d | item xrc=%s vivo=%d id=%u",
		 PathToText(refs.exportSubmenu.path).c_str(), LiveLeafIndex(bar, refs.exportSubmenu),
		 PathToText(refs.exportObjItem.path).c_str(), LiveLeafIndex(bar, refs.exportObjItem),
		 CommandIdAtLabeledPath(bar, refs.exportObjItem.path, refs.exportObjItem.labels));
	LogF("slider_menu: edit mode agora=%d", IsSliderEditModeActive(frame, refs) ? 1 : 0);
	LogF("slider_menu: MFS_GRAYED=0x%08X, entao estado com esse bit = fora do edit mode",
		 static_cast<unsigned>(MFS_GRAYED));
}

bool InvokeMenuCommand(HWND frame, const MenuTrail& trail) {
	if (!frame || trail.empty())
		return false;

	HMENU bar = GetMenu(frame);
	if (!bar)
		return false;

	const UINT id = CommandIdAtLabeledPath(bar, trail.path, trail.labels);
	if (id == 0)
		return false;

	return PostMessageW(frame, WM_COMMAND, MAKEWPARAM(id, 0), 0) != FALSE;
}
