#include "win32/tree.h"

std::vector<HTREEITEM> ChildItems(HWND tree, HTREEITEM parent) {
	std::vector<HTREEITEM> out;
	if (!tree)
		return out;

	HTREEITEM first;
	if (!parent || parent == TVI_ROOT)
		first = reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0));
	else
		first = reinterpret_cast<HTREEITEM>(
			SendMessageW(tree, TVM_GETNEXTITEM, TVGN_CHILD, reinterpret_cast<LPARAM>(parent)));

	for (HTREEITEM it = first; it;
		 it = reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_GETNEXTITEM, TVGN_NEXT, reinterpret_cast<LPARAM>(it))))
		out.push_back(it);

	return out;
}

std::wstring ItemText(HWND tree, HTREEITEM item) {
	if (!tree || !item)
		return std::wstring();

	wchar_t buffer[512] = {};
	TVITEMW info = {};
	info.mask = TVIF_TEXT;
	info.hItem = item;
	info.pszText = buffer;
	info.cchTextMax = static_cast<int>(std::size(buffer));
	if (!SendMessageW(tree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&info)))
		return std::wstring();

	return std::wstring(buffer);
}

bool IsBold(HWND tree, HTREEITEM item) {
	if (!tree || !item)
		return false;

	TVITEMW info = {};
	info.mask = TVIF_STATE;
	info.hItem = item;
	info.stateMask = TVIS_BOLD;
	if (!SendMessageW(tree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&info)))
		return false;

	return (info.state & TVIS_BOLD) != 0;
}

namespace {

HTREEITEM FindBoldImpl(HWND tree, HTREEITEM parent) {
	for (HTREEITEM item : ChildItems(tree, parent)) {
		if (IsBold(tree, item))
			return item;
		if (HTREEITEM found = FindBoldImpl(tree, item))
			return found;
	}
	return nullptr;
}

} // namespace

HTREEITEM FindBoldDescendant(HWND tree) {
	if (!tree)
		return nullptr;
	return FindBoldImpl(tree, TVI_ROOT);
}

void SelectItem(HWND tree, HTREEITEM item) {
	if (!tree || !item)
		return;
	SendMessageW(tree, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(item));
}
