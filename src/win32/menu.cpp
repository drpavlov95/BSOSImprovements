#include "win32/menu.h"

namespace {

bool InRange(HMENU parent, int index) {
	if (!parent || index < 0)
		return false;
	int count = GetMenuItemCount(parent);
	return count > 0 && index < count;
}

} // namespace

HMENU SubMenuAt(HMENU parent, int index) {
	if (!InRange(parent, index))
		return nullptr;
	return GetSubMenu(parent, index);
}

bool IsEnabledAt(HMENU parent, int index) {
	if (!InRange(parent, index))
		return false;

	MENUITEMINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = MIIM_STATE;
	if (!GetMenuItemInfoW(parent, static_cast<UINT>(index), TRUE, &info))
		return false;

	return (info.fState & (MFS_GRAYED | MFS_DISABLED)) == 0;
}

namespace {

// Desce todos os indices menos o ultimo, devolvendo o menu que contem o item
// final. O indice final sai em outIndex.
HMENU ContainerOf(HMENU bar, const std::vector<int>& path, int& outIndex) {
	outIndex = -1;
	if (!bar || path.empty())
		return nullptr;

	HMENU current = bar;
	for (size_t i = 0; i + 1 < path.size(); ++i) {
		current = SubMenuAt(current, path[i]);
		if (!current)
			return nullptr;
	}

	outIndex = path.back();
	return current;
}

} // namespace

HMENU SubMenuAtPath(HMENU bar, const std::vector<int>& path) {
	int index = -1;
	HMENU container = ContainerOf(bar, path, index);
	return container ? SubMenuAt(container, index) : nullptr;
}

bool IsEnabledAtPath(HMENU bar, const std::vector<int>& path) {
	int index = -1;
	HMENU container = ContainerOf(bar, path, index);
	return container ? IsEnabledAt(container, index) : false;
}

UINT CommandIdAtPath(HMENU bar, const std::vector<int>& path) {
	int index = -1;
	HMENU container = ContainerOf(bar, path, index);
	return container ? CommandIdAt(container, index) : 0;
}

UINT CommandIdAt(HMENU parent, int index) {
	if (!InRange(parent, index))
		return 0;

	MENUITEMINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = MIIM_ID;
	if (!GetMenuItemInfoW(parent, static_cast<UINT>(index), TRUE, &info))
		return 0;

	return info.wID;
}
