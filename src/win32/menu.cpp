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
