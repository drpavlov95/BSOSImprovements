// Casamento de hotkey e localizacao da arvore de shapes.
#include "test_util.h"

#include <commctrl.h>

#include "features/hotkeys.h"
#include "features/outfit_tree.h"
#include "win32/tree.h"

TEST(HotkeyMatchingIsExact) {
	Hotkey shiftE = Hotkey{'E', true, false, false};
	TEST_ASSERT(HotkeyMatches(shiftE, 'E', true, false, false));
	TEST_ASSERT(!HotkeyMatches(shiftE, 'E', false, false, false)); // faltou shift
	TEST_ASSERT(!HotkeyMatches(shiftE, 'E', true, true, false));   // ctrl a mais
	TEST_ASSERT(!HotkeyMatches(shiftE, 'E', true, false, true));   // alt a mais
	TEST_ASSERT(!HotkeyMatches(shiftE, 'I', true, false, false));  // tecla errada

	Hotkey plainB = Hotkey{'B', false, false, false};
	TEST_ASSERT(HotkeyMatches(plainB, 'B', false, false, false));
	// Shift+B e Show Bones no vanilla; nao pode ser roubado por um bind de B.
	TEST_ASSERT(!HotkeyMatches(plainB, 'B', true, false, false));
	TEST_ASSERT(!HotkeyMatches(plainB, 'B', false, true, false));

	Hotkey invalid;
	TEST_ASSERT(!HotkeyMatches(invalid, 0, false, false, false));
	TEST_ASSERT(!HotkeyMatches(invalid, 'B', false, false, false));
	return true;
}

namespace {

HWND MakeHost() {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSTreeHost";
		RegisterClassW(&wc);
		INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TREEVIEW_CLASSES};
		InitCommonControlsEx(&icc);
		ready = true;
	}
	return CreateWindowExW(0, L"BSOSTreeHost", L"host", WS_OVERLAPPEDWINDOW,
						   0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND MakeTreeAt(HWND host, int top, bool withStateImages) {
	HWND tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD,
								0, top, 300, 90, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (withStateImages) {
		HIMAGELIST images = ImageList_Create(16, 16, ILC_COLOR32, 2, 2);
		SendMessageW(tree, TVM_SETIMAGELIST, TVSIL_STATE, reinterpret_cast<LPARAM>(images));
	}
	return tree;
}

HTREEITEM InsertItem(HWND tree, HTREEITEM parent, const wchar_t* text) {
	TVINSERTSTRUCTW is = {};
	is.hParent = parent;
	is.hInsertAfter = TVI_LAST;
	is.item.mask = TVIF_TEXT;
	is.item.pszText = const_cast<wchar_t*>(text);
	return reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&is)));
}

void SetBold(HWND tree, HTREEITEM item) {
	TVITEMW bold = {};
	bold.mask = TVIF_STATE;
	bold.hItem = item;
	bold.state = TVIS_BOLD;
	bold.stateMask = TVIS_BOLD;
	SendMessageW(tree, TVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&bold));
}

} // namespace

TEST(FindsShapesTreeByBoldItem) {
	HWND host = MakeHost();
	TEST_ASSERT(host != nullptr);

	// Espelha o Outfit Studio: quatro arvores, todas com wxTR_HIDE_ROOT. Bones
	// vem antes de shapes na enumeracao e tambem tem state image list, entao
	// nem ordem nem state image list sozinhos resolvem.
	HWND bones = MakeTreeAt(host, 0, true);
	HWND shapes = MakeTreeAt(host, 100, true);
	HWND segments = MakeTreeAt(host, 200, false);
	HWND partitions = MakeTreeAt(host, 300, false);
	TEST_ASSERT(bones && shapes && segments && partitions);

	InsertItem(bones, TVI_ROOT, L"NPC Spine [Spn0]");
	HTREEITEM outfit = InsertItem(shapes, TVI_ROOT, L"MeuOutfit");
	InsertItem(shapes, outfit, L"ShapeA");
	HTREEITEM reference = InsertItem(shapes, outfit, L"CBBE 3BA");

	// Sem negrito ainda: cai no fallback, que e a primeira com state image
	// list -- aqui, bones. Comportamento documentado, nao acidental.
	TEST_ASSERT(FindOutfitShapesTree(host) == bones);

	// Com o negrito que o Outfit Studio poe no base shape, a identificacao
	// vira exata, mesmo bones vindo antes.
	SetBold(shapes, reference);
	TEST_ASSERT(FindOutfitShapesTree(host) == shapes);
	TEST_ASSERT(FindReferenceItem(shapes) == reference);

	DestroyWindow(host);
	return true;
}

TEST(SelectReferenceIgnoresProjectWithoutReference) {
	HWND host = MakeHost();
	TEST_ASSERT(host != nullptr);
	HWND shapes = MakeTreeAt(host, 0, true);
	TEST_ASSERT(shapes != nullptr);

	// Com wxTR_HIDE_ROOT nao ha item de raiz nativo: o no do outfit ja e um
	// item de topo, exatamente como no Outfit Studio real.
	HTREEITEM outfit = InsertItem(shapes, TVI_ROOT, L"OutfitSemReference");
	HTREEITEM shapeA = InsertItem(shapes, outfit, L"ShapeA");
	HTREEITEM shapeB = InsertItem(shapes, outfit, L"ShapeB");
	TEST_ASSERT(outfit && shapeA && shapeB);

	// O primeiro shape e filho do no do outfit, nao um item de topo.
	TEST_ASSERT(FirstShapeItem(shapes) == shapeA);

	// Sem item em negrito: precisa recusar, nao cair no primeiro shape.
	TEST_ASSERT(FindReferenceItem(shapes) == nullptr);
	TEST_ASSERT(!SelectReference(host));

	// Marcando o reference, passa a funcionar.
	SetBold(shapes, shapeB);
	TEST_ASSERT(FindReferenceItem(shapes) == shapeB);
	TEST_ASSERT(SelectReference(host));
	TEST_ASSERT(SelectedItem(shapes) == shapeB);

	DestroyWindow(host);
	return true;
}
