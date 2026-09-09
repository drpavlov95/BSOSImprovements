// Como as linhas do dialogo de simetria sao agrupadas e onde a lista esta.
#include "test_util.h"

#include "features/symmetrize_search.h"

namespace {

HWND MakeHost(HWND parent, int x) {
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSAsymHost";
		RegisterClassW(&wc);
		registered = true;
	}

	const DWORD style = parent ? (WS_CHILD | WS_VISIBLE) : WS_OVERLAPPEDWINDOW;
	return CreateWindowExW(0, L"BSOSAsymHost", L"host", style, x, 0, 300, 500,
						   parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// Uma linha como o dialogo monta: caixa de marcacao, nome, media e contagem.
// Os tres textos sao mais baixos que a caixa e ficam centralizados nela, que e
// por que o agrupamento nao pode comparar o topo cru.
void AddRow(HWND host, int y, const wchar_t* name, const wchar_t* average, const wchar_t* count) {
	CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
					0, y, 16, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	CreateWindowExW(0, L"STATIC", name, WS_CHILD | WS_VISIBLE,
					20, y + 4, 120, 12, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	CreateWindowExW(0, L"STATIC", average, WS_CHILD | WS_VISIBLE,
					150, y + 4, 60, 12, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	CreateWindowExW(0, L"STATIC", count, WS_CHILD | WS_VISIBLE,
					220, y + 4, 60, 12, host, nullptr, GetModuleHandleW(nullptr), nullptr);
}

} // namespace

TEST(GroupsAsymRowsByHeight) {
	HWND host = MakeHost(nullptr, 0);
	TEST_ASSERT(host != nullptr);

	// Criadas de baixo para cima: agrupar ou ordenar por ordem de enumeracao
	// devolveria as linhas trocadas.
	AddRow(host, 60, L"NPC R Thigh [RThg]", L"0.03", L"88");
	AddRow(host, 30, L"NPC L Thigh [LThg]", L"0.02", L"42");
	AddRow(host, 0, L"Position", L"0.01", L"17");

	std::vector<AsymRow> rows = GroupRowsByTop(host);
	TEST_ASSERT(rows.size() == 3);

	// De cima para baixo.
	TEST_ASSERT(rows[0].top < rows[1].top);
	TEST_ASSERT(rows[1].top < rows[2].top);

	// Cada linha junta a caixa e as tres celulas dela, e so as dela.
	for (const AsymRow& row : rows)
		TEST_ASSERT(row.cells.size() == 4);

	// O nome pesquisavel traz o texto da linha inteira, entao procurar pelo
	// nome do osso acha a linha dele.
	TEST_ASSERT(rows[0].name.find(L"Position") != std::wstring::npos);
	TEST_ASSERT(rows[1].name.find(L"NPC L Thigh") != std::wstring::npos);
	TEST_ASSERT(rows[2].name.find(L"NPC R Thigh") != std::wstring::npos);

	// E nao mistura linhas: o nome de uma nao pode conter o da outra.
	TEST_ASSERT(rows[1].name.find(L"NPC R Thigh") == std::wstring::npos);
	TEST_ASSERT(rows[2].name.find(L"NPC L Thigh") == std::wstring::npos);

	DestroyWindow(host);
	return true;
}

TEST(TwoChecksAtTheSameHeightAreTwoRows) {
	HWND host = MakeHost(nullptr, 0);
	TEST_ASSERT(host != nullptr);

	// Duas caixas lado a lado. Cada uma e uma linha propria; engolir a segunda
	// como celula da primeira faria o filtro esconder as duas juntas.
	CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
					0, 0, 16, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
					150, 0, 16, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);

	std::vector<AsymRow> rows = GroupRowsByTop(host);
	TEST_ASSERT(rows.size() == 2);
	TEST_ASSERT(rows[0].check != rows[1].check);

	DestroyWindow(host);
	return true;
}

TEST(IgnoresPanelsWithoutChecks) {
	HWND host = MakeHost(nullptr, 0);
	TEST_ASSERT(host != nullptr);

	// So textos: nao ha linha nenhuma para filtrar.
	CreateWindowExW(0, L"STATIC", L"Type", WS_CHILD | WS_VISIBLE,
					0, 0, 60, 12, host, nullptr, GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(GroupRowsByTop(host).empty());
	TEST_ASSERT(GroupRowsByTop(nullptr).empty());

	DestroyWindow(host);
	return true;
}

TEST(ClosesTheGapsTheFilterOpens) {
	// Esta e a feature: sem fechar buraco, procurar "nip" deixava os
	// resultados la embaixo, com um vazio enorme em cima -- que foi
	// exatamente o que apareceu na tela.
	const std::vector<int> tops = {0, 25, 50, 75, 100};

	// So a terceira e a quinta casam: elas sobem para os dois primeiros
	// lugares, que sao os lugares que a lista ja tinha.
	const std::vector<bool> some = {false, false, true, false, true};
	const std::vector<int> placed = CompactRowTops(tops, some);
	TEST_ASSERT(placed.size() == 5);
	TEST_ASSERT(placed[2] == 0);
	TEST_ASSERT(placed[4] == 25);

	// As escondidas ficam onde estavam. Move-las seria trabalho invisivel, e o
	// topo delas e o que permite reconstruir a regua na proxima tecla.
	TEST_ASSERT(placed[0] == 0);
	TEST_ASSERT(placed[1] == 25);
	TEST_ASSERT(placed[3] == 75);

	// Sem filtro, cada linha fica no proprio lugar -- limpar a busca tem que
	// devolver a lista exatamente como estava.
	const std::vector<bool> all(5, true);
	const std::vector<int> untouched = CompactRowTops(tops, all);
	for (size_t i = 0; i < tops.size(); ++i)
		TEST_ASSERT(untouched[i] == tops[i]);

	// Nada casando nao move nada.
	const std::vector<bool> none(5, false);
	const std::vector<int> nothing = CompactRowTops(tops, none);
	for (size_t i = 0; i < tops.size(); ++i)
		TEST_ASSERT(nothing[i] == tops[i]);

	// Espacamento irregular e respeitado: os lugares sao os da lista, nao uma
	// altura fixa inventada aqui.
	const std::vector<int> uneven = {0, 10, 40, 100};
	const std::vector<bool> lastTwo = {false, false, true, true};
	const std::vector<int> onUneven = CompactRowTops(uneven, lastTwo);
	TEST_ASSERT(onUneven[2] == 0);
	TEST_ASSERT(onUneven[3] == 10);

	// Tamanhos incompativeis nao produzem posicao nenhuma: melhor nao mexer
	// que empilhar linha em cima de linha.
	TEST_ASSERT(CompactRowTops(tops, {true, false}).empty());
	TEST_ASSERT(CompactRowTops({}, {}).empty());
	return true;
}

TEST(FindsTheListAreaAmongTheDialogChildren) {
	HWND dlg = MakeHost(nullptr, 0);
	TEST_ASSERT(dlg != nullptr);

	// Uma caixa de marcacao solta no dialogo -- o "Unmatched Vertices" -- nao
	// e a lista.
	CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
					0, 0, 16, 20, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(FindAsymScroll(dlg) == nullptr);

	HWND scroll = MakeHost(dlg, 0);
	TEST_ASSERT(scroll != nullptr);
	AddRow(scroll, 0, L"Position", L"0.01", L"17");
	TEST_ASSERT(FindAsymScroll(dlg) == nullptr); // uma linha so ainda nao e lista

	AddRow(scroll, 30, L"NPC L Thigh [LThg]", L"0.02", L"42");
	TEST_ASSERT(FindAsymScroll(dlg) == scroll);

	// As linhas de slider e de osso ficam um nivel abaixo, cada grupo dentro do
	// seu painel recolhivel. A contagem tem que enxergar ate la.
	HWND collapse = MakeHost(scroll, 0);
	TEST_ASSERT(collapse != nullptr);
	for (int i = 0; i < 5; ++i)
		AddRow(collapse, i * 30, L"Bone", L"0.0", L"0");
	TEST_ASSERT(FindAsymScroll(dlg) == scroll);

	DestroyWindow(dlg);
	return true;
}
