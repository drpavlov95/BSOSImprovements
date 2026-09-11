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

TEST(TheListAreaIsTheDeepestWindowThatStillHoldsEveryRow) {
	// A forma real do dialogo: a lista mora dentro de um wxStaticBox, e a
	// moldura e a area que rola contam exatamente as mesmas linhas porque uma
	// esta dentro da outra. Parar na moldura fazia tudo depois disso cair na
	// janela errada -- inclusive redimensionar a area que rola inteira ate zero.
	HWND dlg = MakeHost(nullptr, 0);
	TEST_ASSERT(dlg != nullptr);

	HWND box = MakeHost(dlg, 0);
	HWND scroll = MakeHost(box, 0);
	TEST_ASSERT(box != nullptr && scroll != nullptr);

	AddRow(scroll, 0, L"Position", L"0.01", L"17");
	AddRow(scroll, 30, L"114 sliders", L"0.02", L"42");

	HWND collapse = MakeHost(scroll, 0);
	TEST_ASSERT(collapse != nullptr);
	for (int i = 0; i < 5; ++i)
		AddRow(collapse, i * 30, L"Bone", L"0.0", L"0");

	// A moldura e a area que rola empatam em sete caixas; ganha a de dentro.
	TEST_ASSERT(FindAsymScroll(dlg) == scroll);

	// E nao a de dentro DELA, que so tem cinco: mais fundo nao basta, tem que
	// continuar carregando tudo.
	TEST_ASSERT(FindAsymScroll(dlg) != collapse);

	DestroyWindow(dlg);
	return true;
}
