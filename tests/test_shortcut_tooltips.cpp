// De onde sai o atalho que vai para o tooltip, e a composicao do texto.
#include "test_util.h"

#include "features/shortcut_tooltips.h"
#include "xrcmap.h"

TEST(FormatsHotkeysTheWayMenusDo) {
	TEST_ASSERT(FormatHotkey(Hotkey{'K', false, false, false}) == L"K");
	TEST_ASSERT(FormatHotkey(Hotkey{'E', true, false, false}) == L"Shift+E");
	TEST_ASSERT(FormatHotkey(Hotkey{'C', false, true, false}) == L"Ctrl+C");
	TEST_ASSERT(FormatHotkey(Hotkey{'K', true, true, true}) == L"Ctrl+Alt+Shift+K");
	TEST_ASSERT(FormatHotkey(Hotkey{'3', false, false, false}) == L"3");

	// Hotkey nao configurada nao pode virar texto nenhum: um "()" vazio no fim
	// do tooltip seria pior que nada.
	TEST_ASSERT(FormatHotkey(Hotkey{}).empty());
	return true;
}

TEST(ComposesTooltipWithoutDuplicating) {
	TEST_ASSERT(ComposeTooltip(L"Shows a transform tool.", L"K") ==
				L"Shows a transform tool. (K)");

	// O ponto do teste: a notificacao pode passar por mais de uma subclasse
	// nossa -- a barra e o pai dela -- e sem idempotencia sairia "(K) (K)".
	TEST_ASSERT(ComposeTooltip(L"Shows a transform tool. (K)", L"K") ==
				L"Shows a transform tool. (K)");

	// Sem atalho, ou sem tooltip de origem, nada e inventado.
	TEST_ASSERT(ComposeTooltip(L"Undo a previous action.", L"") ==
				L"Undo a previous action.");
	TEST_ASSERT(ComposeTooltip(L"", L"K").empty());

	// Tooltip de varias linhas continua de varias linhas, com o atalho no fim.
	TEST_ASSERT(ComposeTooltip(L"Mask vertices.\nHold ALT to remove.", L"1") ==
				L"Mask vertices.\nHold ALT to remove. (1)");

	// Um sufixo parecido mas de outra tecla nao conta como ja aplicado.
	TEST_ASSERT(ComposeTooltip(L"Algo (F)", L"K") == L"Algo (F) (K)");
	return true;
}

TEST(MapsRealToolTooltipsToRealAccelerators) {
	const wchar_t* xrc = L"tests\\data\\OutfitStudio.xrc";
	if (GetFileAttributesW(xrc) == INVALID_FILE_ATTRIBUTES) {
		std::printf("\n      PULADO: copie OutfitStudio.xrc para tests\\data\\ para rodar\n      ");
		return true;
	}

	const XrcShortcuts shortcuts = ResolveXrcShortcuts(xrc);

	// Este e o teste que a versao anterior nao tinha, e por isso ela foi para
	// as maos do usuario sem funcionar: ele percorre a MESMA ligacao que a
	// feature percorre, com o arquivo de verdade.
	//
	// O texto e exatamente o que aparece na tela ao passar o mouse.
	auto transform = shortcuts.toolByTooltip.find(
		L"Shows a transform tool to manipulate shapes and vertices with.");
	TEST_ASSERT(transform != shortcuts.toolByTooltip.end());
	TEST_ASSERT(transform->second == "btnTransform");

	auto perspective = shortcuts.toolByTooltip.find(L"Toggle perspective view.");
	TEST_ASSERT(perspective != shortcuts.toolByTooltip.end());
	TEST_ASSERT(perspective->second == "btnViewPerspective");

	// E o nome leva ao acelerador do item de menu correspondente.
	TEST_ASSERT(shortcuts.acceleratorByName.at("btnTransform") == L"F");
	TEST_ASSERT(shortcuts.acceleratorByName.at("btnViewPerspective") == L"Shift+5");
	TEST_ASSERT(shortcuts.acceleratorByName.at("btnPivot") == L"P");
	TEST_ASSERT(shortcuts.acceleratorByName.at("btnVertexEdit") == L"Q");
	TEST_ASSERT(shortcuts.acceleratorByName.at("btnXMirror") == L"X");
	TEST_ASSERT(shortcuts.acceleratorByName.at("sliderConform") == L"Ctrl+C");

	// Item de menu sem acelerador nao entra: entrar com string vazia faria a
	// reescrita achar que ha atalho e produzir "()".
	TEST_ASSERT(shortcuts.acceleratorByName.find("btnMerge") ==
				shortcuts.acceleratorByName.end());

	// E a cobertura real tem que ser substancial. Se um dia isto cair para
	// zero, alguma coisa mudou no XRC e a feature morreu em silencio -- que e
	// exatamente como ela falhou da primeira vez.
	int matched = 0;
	for (const auto& tool : shortcuts.toolByTooltip) {
		if (tool.second.empty())
			continue;
		if (shortcuts.acceleratorByName.count(tool.second))
			++matched;
	}
	TEST_ASSERT(matched >= 10);
	return true;
}

TEST(ToolTooltipsSurviveTheXrcQuirks) {
	const wchar_t* xrc = L"tests\\data\\OutfitStudio.xrc";
	if (GetFileAttributesW(xrc) == INVALID_FILE_ATTRIBUTES)
		return true;

	const XrcShortcuts shortcuts = ResolveXrcShortcuts(xrc);

	// O "\n" do XRC e literal -- barra e ene -- e o wx o converte em quebra de
	// linha ao carregar. Sem converter tambem aqui, os tooltips de duas linhas
	// nunca casariam com o que aparece na tela.
	auto mask = shortcuts.toolByTooltip.find(
		L"Mask vertices to prevent them from being transformed.\n"
		L"Hold down the ALT key to remove masking.");
	TEST_ASSERT(mask != shortcuts.toolByTooltip.end());
	TEST_ASSERT(mask->second == "btnMaskBrush");

	// Arquivo ausente devolve mapa vazio em vez de explodir.
	TEST_ASSERT(ResolveXrcShortcuts(L"nao_existe.xrc").toolByTooltip.empty());
	TEST_ASSERT(ResolveXrcShortcuts(nullptr).toolByTooltip.empty());
	return true;
}
