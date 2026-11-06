#include "test_util.h"

#include <commctrl.h>

#include "features/slider_data_filter.h"

TEST(SliderDataFilterCanTargetOneColumnOrAllOfThem) {
	std::vector<SliderDataFilterRow> rows = {
		{{L"Arms", L"NetchLeatherBoiled:Armor", L"NetchLeather", L"Arms",
		  L"External", L"EOG 3BA CC", L"Body.osd", L"Found"}, 11, 0},
		{{L"Belly", L"CBBE", L"CBBE", L"Belly",
		  L"Local", L"Project", L"Belly.bsd", L"Found"}, 22, 0},
	};

	auto all = MatchingSliderDataRows(rows, -1, L"3ba");
	TEST_ASSERT(all.size() == 1 && all[0] == 0);

	// O mesmo texto nao esta no nome do slider: limitar a coluna nao pode
	// continuar pesquisando silenciosamente no Data Folder.
	auto sliderOnly = MatchingSliderDataRows(rows, 0, L"3ba");
	TEST_ASSERT(sliderOnly.empty());

	auto shapeOnly = MatchingSliderDataRows(rows, 1, L"cbbe");
	TEST_ASSERT(shapeOnly.size() == 1 && shapeOnly[0] == 1);

	TEST_ASSERT(MatchingSliderDataRows(rows, 0, L"").size() == rows.size());
	return true;
}

TEST(SliderDataColumnsSortWithoutLosingTheNativeRowIdentity) {
	std::vector<SliderDataFilterRow> rows = {
		{{L"belly", L"Shape B"}, 91, 0},
		{{L"Arms", L"Shape C"}, 27, 0},
		{{L"Back", L"Shape A"}, 63, 0},
	};

	SortSliderDataRows(rows, 0, true);
	TEST_ASSERT(rows[0].columns[0] == L"Arms" && rows[0].itemData == 27);
	TEST_ASSERT(rows[1].columns[0] == L"Back" && rows[1].itemData == 63);
	TEST_ASSERT(rows[2].columns[0] == L"belly" && rows[2].itemData == 91);

	SortSliderDataRows(rows, 1, false);
	TEST_ASSERT(rows[0].columns[1] == L"Shape C" && rows[0].itemData == 27);
	TEST_ASSERT(rows[2].columns[1] == L"Shape A" && rows[2].itemData == 63);
	return true;
}

namespace {

HWND MakeSliderDataLikeDialog(bool eightColumns, HWND owner = nullptr) {
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSSliderDataDialog";
		RegisterClassW(&wc);
		registered = true;
	}
	INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
	InitCommonControlsEx(&controls);

	HWND dialog = CreateWindowExW(0, L"BSOSSliderDataDialog", L"",
		WS_OVERLAPPEDWINDOW, 0, 0, 800, 500, owner, nullptr,
		GetModuleHandleW(nullptr), nullptr);
	if (!dialog)
		return nullptr;

	CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 50, 10, 300, 24, dialog,
		nullptr, GetModuleHandleW(nullptr), nullptr);
	for (int i = 0; i < 2; ++i) {
		HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
			WS_CHILD | CBS_DROPDOWNLIST, 400 + i * 120, 10, 100, 100, dialog,
			nullptr, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All"));
		SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"One"));
		SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Two"));
	}
	HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"",
		WS_CHILD | LVS_REPORT, 10, 50, 760, 350, dialog, nullptr,
		GetModuleHandleW(nullptr), nullptr);
	const int count = eightColumns ? 8 : 7;
	for (int i = 0; i < count; ++i) {
		LVCOLUMNW column = {};
		column.mask = LVCF_TEXT | LVCF_WIDTH;
		column.cx = 80;
		column.pszText = const_cast<wchar_t*>(L"Column");
		ListView_InsertColumn(list, i, &column);
	}
	for (int i = 0; i < 4; ++i)
		CreateWindowExW(0, L"BUTTON", L"", WS_CHILD, 10 + i * 90, 420, 80, 24,
			dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
	return dialog;
}

int CountDirectChildrenOfClass(HWND parent, const wchar_t* wanted) {
	int count = 0;
	for (HWND child = GetWindow(parent, GW_CHILD); child;
		 child = GetWindow(child, GW_HWNDNEXT)) {
		wchar_t name[64] = {};
		GetClassNameW(child, name, static_cast<int>(std::size(name)));
		if (_wcsicmp(name, wanted) == 0)
			++count;
	}
	return count;
}

} // namespace

TEST(FindsTheSliderDataDialogByStructureNotByTranslatedText) {
	HWND dialog = MakeSliderDataLikeDialog(true);
	TEST_ASSERT(dialog != nullptr);
	SliderDataDialogControls found = FindSliderDataDialogControls(dialog);
	TEST_ASSERT(found.ok());
	TEST_ASSERT(found.filter != nullptr && found.list != nullptr);
	TEST_ASSERT(found.source != nullptr && found.status != nullptr);
	DestroyWindow(dialog);

	// Sete colunas pode ser outro wxListCtrl qualquer: a assinatura so aceita
	// as oito da tela Slider Data da 5.8.2/5.9.1.
	dialog = MakeSliderDataLikeDialog(false);
	TEST_ASSERT(dialog != nullptr);
	TEST_ASSERT(!FindSliderDataDialogControls(dialog).ok());
	DestroyWindow(dialog);
	return true;
}

TEST(SliderDataHookEnhancesANonStandardWxDialogClass) {
	// O bug visto em campo: wxDialog nao e obrigado a usar a classe #32770.
	// Instala o hook antes de criar uma janela com a mesma estrutura e uma
	// classe arbitraria, exatamente como acontece no ciclo real do wxWidgets.
	HWND frame = MakeSliderDataLikeDialog(false);
	TEST_ASSERT(frame != nullptr);
	TEST_ASSERT(SliderDataFilter::Install(frame));

	HWND dialog = MakeSliderDataLikeDialog(true, frame);
	TEST_ASSERT(dialog != nullptr);
	ShowWindow(dialog, SW_SHOW);
	UpdateWindow(dialog);

	TEST_ASSERT(CountDirectChildrenOfClass(dialog, L"ComboBox") == 3);
	TEST_ASSERT(CountDirectChildrenOfClass(dialog, L"Edit") == 2);

	SliderDataFilter::Uninstall();
	DestroyWindow(dialog);
	DestroyWindow(frame);
	return true;
}
