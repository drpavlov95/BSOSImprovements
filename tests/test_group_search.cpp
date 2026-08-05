// Parsing do filtro de grupos, identificacao do dialogo e validade do template.
#include "test_util.h"

#include <commctrl.h>

#include "features/group_search.h"
#include "win32/winfind.h"

TEST(ParsesFilterLikeBodySlide) {
	// O OnChooseGroups tokeniza em ',' e ';' e faz trim dos dois lados.
	auto tokens = ParseFilterTokens(L"Alpha, Beta;Gamma ,  Delta ");
	TEST_ASSERT(tokens.size() == 4);
	TEST_ASSERT(tokens[0] == L"Alpha");
	TEST_ASSERT(tokens[1] == L"Beta");
	TEST_ASSERT(tokens[2] == L"Gamma");
	TEST_ASSERT(tokens[3] == L"Delta");

	TEST_ASSERT(ParseFilterTokens(L"").empty());
	TEST_ASSERT(ParseFilterTokens(L"   ").empty());
	TEST_ASSERT(ParseFilterTokens(L",,;;").empty()); // tokens vazios caem fora

	// Nomes de grupo reais tem espacos, parenteses e underscores.
	auto real = ParseFilterTokens(L"0CCE_3BA_Female(No Accessories), Rley HIMBO Refits");
	TEST_ASSERT(real.size() == 2);
	TEST_ASSERT(real[0] == L"0CCE_3BA_Female(No Accessories)");
	TEST_ASSERT(real[1] == L"Rley HIMBO Refits");

	auto single = ParseFilterTokens(L"Rley HIMBO Refits");
	TEST_ASSERT(single.size() == 1 && single[0] == L"Rley HIMBO Refits");
	return true;
}

TEST(JoinRoundTripsThroughParse) {
	std::vector<std::wstring> input{L"Alpha", L"Beta Gamma", L"0CCE_3BA_Female"};
	TEST_ASSERT(JoinFilterTokens(input) == L"Alpha, Beta Gamma, 0CCE_3BA_Female");
	TEST_ASSERT(ParseFilterTokens(JoinFilterTokens(input)) == input);
	TEST_ASSERT(JoinFilterTokens({}).empty());
	TEST_ASSERT(ParseFilterTokens(JoinFilterTokens({})).empty());
	return true;
}

TEST(FilterMatchIsCaseInsensitiveSubstring) {
	TEST_ASSERT(MatchesFilter(L"0CCE_3BA_Female", L"3ba"));
	TEST_ASSERT(MatchesFilter(L"0CCE_3BA_Female", L"3BA"));
	TEST_ASSERT(MatchesFilter(L"0CCE_3BA_Female", L"_female"));
	TEST_ASSERT(MatchesFilter(L"Rley HIMBO Refits", L"himbo"));

	// Consulta vazia mostra tudo -- e o estado em que o dialogo abre.
	TEST_ASSERT(MatchesFilter(L"qualquer coisa", L""));

	TEST_ASSERT(!MatchesFilter(L"0CCE_Male", L"3ba"));
	TEST_ASSERT(!MatchesFilter(L"", L"x"));
	return true;
}

namespace {

// Monta um dialogo com a mistura de controles pedida, como os do BodySlide.
// lastItem: o texto do ultimo item da lista. O Choose Groups sempre termina em
// "Unassigned"; e isso, e nao a estrutura, que o identifica.
HWND MakeDialogWith(int listBoxes, int buttons, int edits, int combos, bool ownerDrawnList,
					int items, const wchar_t* lastItem = L"Unassigned") {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSFakeDlg";
		RegisterClassW(&wc);
		ready = true;
	}
	HWND dlg = CreateWindowExW(0, L"BSOSFakeDlg", L"dlg", WS_OVERLAPPEDWINDOW,
							   0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

	for (int i = 0; i < listBoxes; ++i) {
		DWORD style = WS_CHILD | LBS_HASSTRINGS | LBS_NOTIFY;
		if (ownerDrawnList)
			style |= LBS_OWNERDRAWFIXED;
		HWND lb = CreateWindowExW(0, L"LISTBOX", L"", style, 0, 0, 200, 100, dlg,
								  nullptr, GetModuleHandleW(nullptr), nullptr);
		for (int n = 0; n < items; ++n) {
			const wchar_t* text = (n == items - 1) ? lastItem : L"grupo";
			SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
		}
	}
	for (int i = 0; i < buttons; ++i)
		CreateWindowExW(0, L"BUTTON", L"b", WS_CHILD, 0, 0, 40, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
	for (int i = 0; i < edits; ++i)
		CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 40, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
	for (int i = 0; i < combos; ++i)
		CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0, 0, 40, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);

	return dlg;
}

INT_PTR CALLBACK NullDialogProc(HWND, UINT, WPARAM, LPARAM) {
	return FALSE;
}

} // namespace

TEST(OnlyTheChooseGroupsDialogIsIntercepted) {
	// O verdadeiro: um texto, uma lista de marcacao, OK/Cancel, e "Unassigned"
	// como ultimo item.
	HWND real = MakeDialogWith(1, 2, 0, 0, true, 5);
	TEST_ASSERT(FindChooseGroupsListBox(real) != nullptr);

	// dlgBatchBuild, o caso reportado por usuario. Tem EXATAMENTE a mesma
	// estrutura: uma lista de marcacao e dois botoes. Nenhuma contagem de
	// controles separa os dois -- so o conteudo. A tentativa anterior de
	// distinguir por estrutura falhou justamente aqui, porque BatchBuild.xrc
	// declara dois dialogos e eu somei os controles dos dois.
	HWND batchBuild = MakeDialogWith(1, 2, 0, 0, true, 5, L"Rley HIMBO Armor");
	TEST_ASSERT(FindChooseGroupsListBox(batchBuild) == nullptr);

	// Uma lista de grupos que por acaso tenha "Unassigned" no meio, mas nao no
	// fim, tambem nao serve: o OnChooseGroups acrescenta depois do loop.
	HWND unassignedNotLast = MakeDialogWith(1, 2, 0, 0, true, 5, L"outra coisa");
	SendMessageW(FindWindowExW(unassignedNotLast, nullptr, L"LISTBOX", nullptr), LB_INSERTSTRING,
				 0, reinterpret_cast<LPARAM>(L"Unassigned"));
	TEST_ASSERT(FindChooseGroupsListBox(unassignedNotLast) == nullptr);

	// SliderDataImport: duas listas.
	HWND sliderImport = MakeDialogWith(2, 2, 0, 0, true, 5);
	TEST_ASSERT(FindChooseGroupsListBox(sliderImport) == nullptr);

	// Lista comum, sem marcacao: nao e um wxCheckListBox.
	HWND plainList = MakeDialogWith(1, 2, 0, 0, false, 5);
	TEST_ASSERT(FindChooseGroupsListBox(plainList) == nullptr);

	// Lista vazia: nao ha grupos para ler.
	HWND emptyList = MakeDialogWith(1, 2, 0, 0, true, 0);
	TEST_ASSERT(FindChooseGroupsListBox(emptyList) == nullptr);

	for (HWND h : {real, batchBuild, unassignedNotLast, sliderImport, plainList, emptyList})
		DestroyWindow(h);
	return true;
}

TEST(DialogTemplateCreatesAllControls) {
	// Um DLGTEMPLATE montado a mao so revela erro de alinhamento ou de
	// contagem de itens na hora de criar o dialogo. Melhor descobrir aqui.
	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES};
	InitCommonControlsEx(&icc);

	std::vector<BYTE> tpl = BuildGroupsDialogTemplate();
	TEST_ASSERT(!tpl.empty());

	HWND dlg = CreateDialogIndirectParamW(GetModuleHandleW(nullptr),
										  reinterpret_cast<LPCDLGTEMPLATEW>(tpl.data()),
										  nullptr, NullDialogProc, 0);
	TEST_ASSERT(dlg != nullptr);

	TEST_ASSERT(GetDlgItem(dlg, kGroupSearchEdit) != nullptr);
	TEST_ASSERT(GetDlgItem(dlg, kGroupSearchList) != nullptr);
	TEST_ASSERT(GetDlgItem(dlg, kGroupSearchCheckVisible) != nullptr);
	TEST_ASSERT(GetDlgItem(dlg, kGroupSearchClearAll) != nullptr);
	TEST_ASSERT(GetDlgItem(dlg, kGroupSearchCounter) != nullptr);
	TEST_ASSERT(GetDlgItem(dlg, IDOK) != nullptr);
	TEST_ASSERT(GetDlgItem(dlg, IDCANCEL) != nullptr);

	wchar_t cls[64] = {};
	GetClassNameW(GetDlgItem(dlg, kGroupSearchList), cls, 64);
	TEST_ASSERT(_wcsicmp(cls, WC_LISTVIEWW) == 0);

	GetClassNameW(GetDlgItem(dlg, kGroupSearchEdit), cls, 64);
	TEST_ASSERT(_wcsicmp(cls, L"Edit") == 0);

	HWND list = GetDlgItem(dlg, kGroupSearchList);
	ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES);
	TEST_ASSERT((ListView_GetExtendedListViewStyle(list) & LVS_EX_CHECKBOXES) != 0);

	DestroyWindow(dlg);
	return true;
}

// Escolher a caixa de filtro certa quando ha mais de uma.
//
// O 5.8.2 acrescentou uma segunda wxSearchCtrl ("Filter outfits...") irma da de
// grupos. Ela e criada DEPOIS, e no Windows a janela criada por ultimo entra no
// topo do z-order -- que e a ordem em que os filhos sao enumerados. Escolher
// "o primeiro Edit" passou a escrever os nomes dos grupos no filtro de outfits,
// e a lista de outfits esvaziava em vez de filtrar.
TEST(PicksTheGroupFilterAndNotTheOutfitFilter) {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSFilterHost";
		RegisterClassW(&wc);
		ready = true;
	}

	HWND frame = CreateWindowExW(0, L"BSOSFilterHost", L"frame", WS_OVERLAPPEDWINDOW,
								 0, 0, 500, 300, nullptr, nullptr,
								 GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(frame != nullptr);

	// A de outfits primeiro, de proposito: assim a varredura a encontra antes,
	// e escolher por ordem daria a caixa errada. Qual das duas vem primeiro no
	// BodySlide de verdade nao da para reproduzir aqui -- o AttachUnknownControl
	// reparenta as duas para paineis diferentes, e a ordem passa a depender da
	// arvore de paineis. E justamente por nao dar para saber que a escolha nao
	// pode se apoiar em ordem.
	HWND outfits = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_VISIBLE,
								   0, 30, 200, 24, frame, nullptr,
								   GetModuleHandleW(nullptr), nullptr);
	HWND groups = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_VISIBLE,
								  0, 0, 200, 24, frame, nullptr,
								  GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(groups && outfits);

	SendMessageW(groups, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Filter groups..."));
	SendMessageW(outfits, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Filter outfits..."));

	const std::vector<std::wstring> known = {L"3BA", L"HIMBO", L"Unassigned"};

	// Premissa afirmada, para o teste nao ser vazio: a varredura encontra a
	// caixa errada primeiro. Sem isso, acertar seria sorte da ordem.
	TEST_ASSERT(FindDescendantByClass(frame, L"Edit", 0) == outfits);

	// Ambas vazias -- nenhum conteudo prova nada. So a dica desempata, e sem
	// ela a escolha cairia na primeira encontrada, que e a de outfits.
	TEST_ASSERT(FindGroupFilterEdit(frame, known) == groups);

	// Conteudo e prova mais forte que a dica: nomes que existem todos na lista
	// de grupos so podem estar na caixa de grupos.
	SetWindowTextW(groups, L"3BA, HIMBO");
	TEST_ASSERT(FindGroupFilterEdit(frame, known) == groups);

	// Texto que nao e nome de grupo nao rouba a escolha.
	SetWindowTextW(outfits, L"dragonbone");
	TEST_ASSERT(FindGroupFilterEdit(frame, known) == groups);

	// Sem dica reconhecivel -- instalacao traduzida -- e sem conteudo que prove,
	// devolve nulo de proposito. Escrever num palpite e pior que nao escrever:
	// o palpite cai num campo de valor de slider e estraga o valor do usuario.
	SetWindowTextW(groups, L"");
	SetWindowTextW(outfits, L"");
	SendMessageW(groups, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Filtrar grupos..."));
	SendMessageW(outfits, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Filtrar roupas..."));
	TEST_ASSERT(FindGroupFilterEdit(frame, known) == nullptr);

	DestroyWindow(frame);
	return true;
}

// O caso real da instalacao do usuario: cada slider tem a sua caixinha de
// porcentagem, que tambem e um Edit, e elas aparecem na varredura ANTES das
// caixas de busca. Com um teto de 16 na varredura, nenhuma caixa de busca era
// sequer examinada e o filtro ia parar dentro do campo de valor de um slider.
TEST(SliderValueBoxesDoNotHijackTheFilter) {
	static bool ready = false;
	if (!ready) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSSliderHost";
		RegisterClassW(&wc);
		ready = true;
	}

	HWND frame = CreateWindowExW(0, L"BSOSSliderHost", L"frame", WS_OVERLAPPEDWINDOW,
								 0, 0, 500, 300, nullptr, nullptr,
								 GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(frame != nullptr);

	// 40 campos de slider primeiro -- bem mais que o teto de 16 que havia.
	for (int i = 0; i < 40; ++i) {
		HWND box = CreateWindowExW(0, L"Edit", L"0%", WS_CHILD, 0, i * 4, 40, 20,
								   frame, nullptr, GetModuleHandleW(nullptr), nullptr);
		TEST_ASSERT(box != nullptr);
	}

	HWND groups = CreateWindowExW(0, L"Edit", L"", WS_CHILD, 0, 200, 200, 24, frame,
								  nullptr, GetModuleHandleW(nullptr), nullptr);
	SendMessageW(groups, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"Filter groups..."));

	const std::vector<std::wstring> known = {L"MPP CBBE Reference", L"MPP HIMBO Reference"};

	HWND chosen = FindGroupFilterEdit(frame, known);
	TEST_ASSERT(chosen == groups);

	// E o texto do filtro nao pode ser confundido com conteudo de slider: "0%"
	// nao e nome de grupo, entao nenhum campo de slider "prova" ser a caixa.
	SetWindowTextW(groups, L"MPP CBBE Reference, MPP HIMBO Reference");
	TEST_ASSERT(FindGroupFilterEdit(frame, known) == groups);

	DestroyWindow(frame);
	return true;
}
