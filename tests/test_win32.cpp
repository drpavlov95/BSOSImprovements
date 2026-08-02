// Helpers de tree, menu e busca de janelas, contra controles nativos reais.
#include "test_util.h"

#include <commctrl.h>

#include "win32/menu.h"
#include "win32/tree.h"
#include "win32/winfind.h"

namespace {

HWND MakeHostWindow() {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSTestHost";
		RegisterClassW(&wc);

		INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
		InitCommonControlsEx(&icc);
		ready = true;
	}
	return CreateWindowExW(0, L"BSOSTestHost", L"host", WS_OVERLAPPEDWINDOW,
						   0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND MakeTree(HWND host) {
	return CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | TVS_HASBUTTONS | TVS_LINESATROOT,
						   0, 0, 300, 200, host, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HTREEITEM TreeInsert(HWND tree, HTREEITEM parent, const wchar_t* text) {
	TVINSERTSTRUCTW is = {};
	is.hParent = parent;
	is.hInsertAfter = TVI_LAST;
	is.item.mask = TVIF_TEXT;
	is.item.pszText = const_cast<wchar_t*>(text);
	return reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&is)));
}

void SetBold(HWND tree, HTREEITEM item, bool bold) {
	TVITEMW it = {};
	it.mask = TVIF_STATE;
	it.hItem = item;
	it.state = bold ? TVIS_BOLD : 0;
	it.stateMask = TVIS_BOLD;
	SendMessageW(tree, TVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&it));
}

} // namespace

TEST(TreeHelpersReadBoldAndSelect) {
	HWND host = MakeHostWindow();
	TEST_ASSERT(host != nullptr);
	HWND tree = MakeTree(host);
	TEST_ASSERT(tree != nullptr);

	HTREEITEM root = TreeInsert(tree, TVI_ROOT, L"Shapes");
	HTREEITEM outfit = TreeInsert(tree, root, L"MyOutfit");
	HTREEITEM shapeA = TreeInsert(tree, outfit, L"ShapeA");
	HTREEITEM ref = TreeInsert(tree, outfit, L"CBBE 3BA");
	HTREEITEM shapeB = TreeInsert(tree, outfit, L"ShapeB");
	TEST_ASSERT(root && outfit && shapeA && ref && shapeB);

	TEST_ASSERT(ChildItems(tree, outfit).size() == 3);
	TEST_ASSERT(ChildItems(tree, TVI_ROOT).size() == 1);

	// Sem nenhum negrito ainda: precisa devolver nullptr, nao o primeiro item.
	TEST_ASSERT(FindBoldDescendant(tree) == nullptr);

	SetBold(tree, ref, true);
	TEST_ASSERT(!IsBold(tree, shapeA));
	TEST_ASSERT(IsBold(tree, ref));
	TEST_ASSERT(FindBoldDescendant(tree) == ref);

	SelectItem(tree, ref);
	TEST_ASSERT(reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_GETNEXTITEM, TVGN_CARET, 0)) == ref);
	TEST_ASSERT(IsSelected(tree, ref));
	TEST_ASSERT(ItemText(tree, ref) == L"CBBE 3BA");

	RECT rc = {};
	TEST_ASSERT(ItemRect(tree, ref, rc));

	SetBold(tree, ref, false);
	TEST_ASSERT(FindBoldDescendant(tree) == nullptr);

	DestroyWindow(host);
	return true;
}

TEST(MenuHelpersCountSeparatorsAndReadState) {
	HMENU bar = CreateMenu();
	HMENU sub = CreatePopupMenu();
	AppendMenuW(sub, MF_STRING, 1001, L"Item Um");
	AppendMenuW(sub, MF_SEPARATOR, 0, nullptr); // separador ocupa posicao
	AppendMenuW(sub, MF_STRING, 1002, L"Item Tres");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Menu");

	TEST_ASSERT(SubMenuAt(bar, 0) == sub);
	TEST_ASSERT(SubMenuAt(bar, 1) == nullptr);

	TEST_ASSERT(CommandIdAt(sub, 0) == 1001);
	TEST_ASSERT(CommandIdAt(sub, 2) == 1002); // indice 1 e o separador

	TEST_ASSERT(IsEnabledAt(sub, 0));
	EnableMenuItem(sub, 0, MF_BYPOSITION | MF_GRAYED);
	TEST_ASSERT(!IsEnabledAt(sub, 0));
	EnableMenuItem(sub, 0, MF_BYPOSITION | MF_ENABLED);
	TEST_ASSERT(IsEnabledAt(sub, 0));

	// Versoes por caminho, como o xrcmap produz.
	TEST_ASSERT(CommandIdAtPath(bar, {0, 2}) == 1002);
	TEST_ASSERT(SubMenuAtPath(bar, {0}) == sub);
	TEST_ASSERT(IsEnabledAtPath(bar, {0, 0}));
	TEST_ASSERT(CommandIdAtPath(bar, {0, 99}) == 0);
	TEST_ASSERT(CommandIdAtPath(bar, {}) == 0);

	DestroyMenu(bar);
	return true;
}

TEST(FindsControlsByClassInHierarchy) {
	HWND host = MakeHostWindow();
	TEST_ASSERT(host != nullptr);

	HWND panel = CreateWindowExW(0, L"BSOSTestHost", L"panel", WS_CHILD,
								 0, 0, 200, 200, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
								0, 0, 100, 20, panel, nullptr, GetModuleHandleW(nullptr), nullptr);
	HWND tree = MakeTree(panel);
	TEST_ASSERT(panel && edit && tree);

	// Filho direto: o edit esta sob o panel, nao sob o host.
	TEST_ASSERT(FindChildByClass(host, L"Edit") == nullptr);
	TEST_ASSERT(FindChildByClass(panel, L"Edit") == edit);

	// Descendente: alcanca atraves do panel.
	TEST_ASSERT(FindDescendantByClass(host, L"Edit") == edit);
	TEST_ASSERT(FindDescendantByClass(host, WC_TREEVIEWW) == tree);
	TEST_ASSERT(FindDescendantByClass(host, L"NaoExiste") == nullptr);

	TEST_ASSERT(ClassOf(edit) == L"Edit");
	TEST_ASSERT(ChildrenOf(panel).size() == 2);

	DestroyWindow(host);
	return true;
}

TEST(DetectsTextInputFocus) {
	HWND host = MakeHostWindow();
	TEST_ASSERT(host != nullptr);
	ShowWindow(host, SW_SHOWNA); // SetFocus exige janela realizada
	HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
								0, 0, 100, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	HWND tree = MakeTree(host);
	ShowWindow(tree, SW_SHOW);
	TEST_ASSERT(edit && tree);

	SetFocus(edit);
	TEST_ASSERT(GetFocus() == edit);
	TEST_ASSERT(IsTextInputFocused()); // digitar aqui nao pode virar atalho

	SetFocus(tree);
	TEST_ASSERT(GetFocus() == tree);
	TEST_ASSERT(!IsTextInputFocused()); // aqui o atalho vale

	SetFocus(nullptr);
	TEST_ASSERT(!IsTextInputFocused());

	DestroyWindow(host);
	return true;
}
