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

UINT StateAtPath(HMENU bar, const std::vector<int>& path) {
	int index = -1;
	HMENU container = ContainerOf(bar, path, index);
	if (!container || !InRange(container, index))
		return 0xFFFFFFFF;

	MENUITEMINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = MIIM_STATE;
	if (!GetMenuItemInfoW(container, static_cast<UINT>(index), TRUE, &info))
		return 0xFFFFFFFF;

	return info.fState;
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

std::wstring MenuTextAt(HMENU parent, int index) {
	if (!InRange(parent, index))
		return std::wstring();

	// Duas passadas: a primeira so para saber o tamanho. dwTypeData nulo com
	// MIIM_STRING preenche cch e nao escreve nada.
	MENUITEMINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = MIIM_STRING;
	if (!GetMenuItemInfoW(parent, static_cast<UINT>(index), TRUE, &info) || info.cch == 0)
		return std::wstring();

	std::wstring buffer(static_cast<size_t>(info.cch) + 1, L'\0');
	info.dwTypeData = buffer.data();
	info.cch = static_cast<UINT>(buffer.size());
	if (!GetMenuItemInfoW(parent, static_cast<UINT>(index), TRUE, &info))
		return std::wstring();
	buffer.resize(info.cch);

	std::wstring clean;
	for (wchar_t c : buffer) {
		if (c == L'\t') // acelerador: nao faz parte do nome
			break;
		if (c == L'&') // mnemonico
			continue;
		clean.push_back(c);
	}

	size_t begin = clean.find_first_not_of(L" \r\n");
	if (begin == std::wstring::npos)
		return std::wstring();
	size_t end = clean.find_last_not_of(L" \r\n");
	return clean.substr(begin, end - begin + 1);
}

namespace {

// Indice do item com este rotulo. -1 se nao houver, ou se houver mais de um --
// ambiguidade nao pode escolher sozinha, e melhor deixar a posicao decidir.
int IndexByLabel(HMENU parent, const std::wstring& label, bool mustBePopup) {
	if (!parent || label.empty())
		return -1;

	const int count = GetMenuItemCount(parent);
	int found = -1;
	for (int i = 0; i < count; ++i) {
		if (mustBePopup && !GetSubMenu(parent, i))
			continue;
		if (MenuTextAt(parent, i) != label)
			continue;
		if (found != -1)
			return -1;
		found = i;
	}
	return found;
}

int StepIndex(HMENU parent, int hint, const std::wstring& label, bool mustBePopup) {
	const int byLabel = IndexByLabel(parent, label, mustBePopup);
	return (byLabel >= 0) ? byLabel : hint;
}

} // namespace

HMENU ContainerAtLabeledPath(HMENU bar, const std::vector<int>& path,
							 const std::vector<std::wstring>& labels, int& outIndex) {
	outIndex = -1;
	if (!bar || path.empty())
		return nullptr;

	HMENU current = bar;
	for (size_t i = 0; i + 1 < path.size(); ++i) {
		const std::wstring label = (i < labels.size()) ? labels[i] : std::wstring();
		// Nivel intermediario e sempre submenu, entao exigir popup descarta de
		// saida qualquer item comum que por acaso tenha o mesmo texto.
		current = SubMenuAt(current, StepIndex(current, path[i], label, true));
		if (!current)
			return nullptr;
	}

	const size_t last = path.size() - 1;
	const std::wstring label = (last < labels.size()) ? labels[last] : std::wstring();
	outIndex = StepIndex(current, path[last], label, false);
	return current;
}

bool IsEnabledAtLabeledPath(HMENU bar, const std::vector<int>& path,
							const std::vector<std::wstring>& labels) {
	int index = -1;
	HMENU container = ContainerAtLabeledPath(bar, path, labels, index);
	return container ? IsEnabledAt(container, index) : false;
}

UINT CommandIdAtLabeledPath(HMENU bar, const std::vector<int>& path,
							const std::vector<std::wstring>& labels) {
	int index = -1;
	HMENU container = ContainerAtLabeledPath(bar, path, labels, index);
	return container ? CommandIdAt(container, index) : 0;
}
