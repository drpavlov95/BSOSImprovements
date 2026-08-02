// Identificacao do dialogo de Batch Build e busca na lista de outfits.
#include "test_util.h"

#include "features/batch_build_search.h"

namespace {

HWND MakeDialog(int listBoxes, int buttons, int edits, int combos, bool ownerDrawn,
				const std::vector<const wchar_t*>& items) {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSBatchDlg";
		RegisterClassW(&wc);
		ready = true;
	}
	HWND dlg = CreateWindowExW(0, L"BSOSBatchDlg", L"dlg", WS_OVERLAPPEDWINDOW,
							   0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

	for (int i = 0; i < listBoxes; ++i) {
		DWORD style = WS_CHILD | LBS_HASSTRINGS | LBS_NOTIFY;
		if (ownerDrawn)
			style |= LBS_OWNERDRAWFIXED;
		HWND lb = CreateWindowExW(0, L"LISTBOX", L"", style, 0, 0, 200, 100, dlg,
								  nullptr, GetModuleHandleW(nullptr), nullptr);
		for (const wchar_t* t : items)
			SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
	}
	for (int i = 0; i < buttons; ++i)
		CreateWindowExW(0, L"BUTTON", L"b", WS_CHILD, 0, 0, 40, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
	for (int i = 0; i < edits; ++i)
		CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 40, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
	for (int i = 0; i < combos; ++i)
		CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0, 0, 40, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);

	return dlg;
}

} // namespace

TEST(TellsBatchBuildApartFromChooseGroups) {
	// Os dois tem exatamente a mesma estrutura -- uma lista de marcacao e dois
	// botoes -- e so o conteudo os separa.
	HWND batchBuild = MakeDialog(1, 2, 0, 0, true, {L"Armor A", L"Armor B", L"Rley HIMBO"});
	TEST_ASSERT(FindBatchBuildListBox(batchBuild) != nullptr);

	// Choose Groups: mesma estrutura, mas termina em "Unassigned".
	HWND chooseGroups = MakeDialog(1, 2, 0, 0, true, {L"Grupo A", L"Grupo B", L"Unassigned"});
	TEST_ASSERT(FindBatchBuildListBox(chooseGroups) == nullptr);

	// dlgSavePreset e dlgSliderProp tem campo de texto.
	HWND withEdit = MakeDialog(1, 2, 1, 0, true, {L"Armor A"});
	TEST_ASSERT(FindBatchBuildListBox(withEdit) == nullptr);

	// dlgSettings tem combos.
	HWND withCombo = MakeDialog(1, 2, 0, 3, true, {L"Armor A"});
	TEST_ASSERT(FindBatchBuildListBox(withCombo) == nullptr);

	// dlgSliderDataImport tem duas listas.
	HWND twoLists = MakeDialog(2, 2, 0, 0, true, {L"Armor A"});
	TEST_ASSERT(FindBatchBuildListBox(twoLists) == nullptr);

	// dlgPackProjects tem quatro botoes.
	HWND fourButtons = MakeDialog(1, 4, 0, 0, true, {L"Armor A"});
	TEST_ASSERT(FindBatchBuildListBox(fourButtons) == nullptr);

	// Lista comum, sem marcacao.
	HWND plain = MakeDialog(1, 2, 0, 0, false, {L"Armor A"});
	TEST_ASSERT(FindBatchBuildListBox(plain) == nullptr);

	// Lista vazia.
	HWND empty = MakeDialog(1, 2, 0, 0, true, {});
	TEST_ASSERT(FindBatchBuildListBox(empty) == nullptr);

	for (HWND h : {batchBuild, chooseGroups, withEdit, withCombo, twoLists, fourButtons, plain, empty})
		DestroyWindow(h);
	return true;
}

TEST(FindsMatchingOutfits) {
	std::vector<std::wstring> outfits{
		L"Rley HIMBO Vanilla - Feet",
		L"CBBE 3BA Armor",
		L"Rley HIMBO Armors - Heavy",
		L"Vanilla Clothes",
	};

	// Case-insensitive, substring, na ordem da lista.
	auto himbo = MatchingItems(outfits, L"himbo");
	TEST_ASSERT(himbo.size() == 2);
	TEST_ASSERT(himbo[0] == 0 && himbo[1] == 2);

	auto vanilla = MatchingItems(outfits, L"Vanilla");
	TEST_ASSERT(vanilla.size() == 2);
	TEST_ASSERT(vanilla[0] == 0 && vanilla[1] == 3);

	TEST_ASSERT(MatchingItems(outfits, L"naoexiste").empty());

	// Consulta vazia nao casa com nada, entao apagar a busca nao faz a
	// selecao pular para lugar nenhum.
	TEST_ASSERT(MatchingItems(outfits, L"").empty());
	return true;
}
