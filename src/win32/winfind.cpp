#include "win32/winfind.h"

std::vector<HWND> ChildrenOf(HWND parent) {
	std::vector<HWND> out;
	if (!parent)
		return out;
	for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
		out.push_back(child);
	return out;
}

std::wstring ClassOf(HWND hwnd) {
	if (!hwnd)
		return std::wstring();
	wchar_t buf[128] = {};
	int n = GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
	return std::wstring(buf, n > 0 ? n : 0);
}

HWND FindChildByClass(HWND parent, const wchar_t* cls, int nth) {
	if (!parent || !cls)
		return nullptr;
	int seen = 0;
	for (HWND child : ChildrenOf(parent)) {
		// Nomes de classe de janela sao case-insensitive.
		if (_wcsicmp(ClassOf(child).c_str(), cls) == 0 && seen++ == nth)
			return child;
	}
	return nullptr;
}

namespace {

// Busca em profundidade, pre-ordem, seguindo a z-order dos irmaos.
HWND FindDescendantImpl(HWND root, const wchar_t* cls, int nth, int& seen) {
	for (HWND child : ChildrenOf(root)) {
		if (_wcsicmp(ClassOf(child).c_str(), cls) == 0 && seen++ == nth)
			return child;
		if (HWND found = FindDescendantImpl(child, cls, nth, seen))
			return found;
	}
	return nullptr;
}

} // namespace

HWND FindDescendantByClass(HWND root, const wchar_t* cls, int nth) {
	if (!root || !cls)
		return nullptr;
	int seen = 0;
	return FindDescendantImpl(root, cls, nth, seen);
}

bool IsTextInputFocused() {
	HWND focus = GetFocus();
	if (!focus)
		return false;

	std::wstring cls = ClassOf(focus);
	if (_wcsicmp(cls.c_str(), L"Edit") == 0)
		return true;
	if (_wcsnicmp(cls.c_str(), L"RichEdit", 8) == 0)
		return true;

	// O campo editavel de um combobox e um Edit filho -- ja coberto acima --
	// mas um combobox com foco proprio tambem aceita digitacao.
	if (_wcsicmp(cls.c_str(), L"ComboBox") == 0)
		return true;

	return false;
}
