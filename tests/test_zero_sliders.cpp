// Onde as barras vao ao serem zeradas, e qual painel e o painel de sliders.
#include "test_util.h"

#include <commctrl.h>

#include "features/zero_sliders.h"

TEST(ZeroLandsInsideTheSliderRange) {
	// O caso normal dos dois programas: sliders de 0 a 100.
	TEST_ASSERT(ZeroTargetFor(0, 100) == 0);

	// As barras de pose vao de -300 a 300, e ali o zero e o meio.
	TEST_ASSERT(ZeroTargetFor(-300, 300) == 0);
	TEST_ASSERT(ZeroTargetFor(-1000, 1000) == 0);

	// Faixa que nem passa pelo zero cai no extremo mais proximo. Mandar zero
	// para um controle que nao o aceita gravaria um valor fora da faixa.
	TEST_ASSERT(ZeroTargetFor(10, 50) == 10);
	TEST_ASSERT(ZeroTargetFor(-50, -10) == -10);

    // Faixa degenerada nao pode virar um valor inventado.
	TEST_ASSERT(ZeroTargetFor(5, 5) == 5);
	TEST_ASSERT(ZeroTargetFor(100, 0) == 100);
	return true;
}

namespace {

HWND MakePanel(HWND parent, bool scrolls, int sliderCount, int x) {
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSZeroHost";
		RegisterClassW(&wc);
		registered = true;
	}

	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
	InitCommonControlsEx(&icc);

	DWORD style = WS_CHILD | WS_VISIBLE;
	if (scrolls)
		style |= WS_VSCROLL;

	HWND panel = CreateWindowExW(0, L"BSOSZeroHost", L"", style, x, 0, 200, 400,
								 parent, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!panel)
		return nullptr;

	for (int i = 0; i < sliderCount; ++i) {
		CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE,
						0, i * 25, 150, 20, panel, nullptr, GetModuleHandleW(nullptr), nullptr);
	}
	return panel;
}

HWND MakeFrame() {
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSZeroFrame";
		RegisterClassW(&wc);
		registered = true;
	}
	// Longe da tela, mas EXIBIDO: PickSliderHost pergunta por IsWindowVisible,
	// que so responde sim quando a cadeia inteira de pais esta visivel. E essa
	// pergunta que descarta o painel recolhido no programa de verdade, entao um
	// frame nunca exibido mediria outra coisa.
	HWND frame = CreateWindowExW(0, L"BSOSZeroFrame", L"frame", WS_OVERLAPPEDWINDOW,
								 -4000, -4000, 800, 600, nullptr, nullptr,
								 GetModuleHandleW(nullptr), nullptr);
	if (frame)
		ShowWindow(frame, SW_SHOWNA);
	return frame;
}

} // namespace

TEST(PicksTheScrollingPanelOverABiggerOne) {
	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);

	// O painel de sliders rola e pode ter poucas barras -- um outfit com tres
	// sliders. O painel de luzes do Outfit Studio nao rola e tem seis.
	HWND sliderPanel = MakePanel(frame, true, 3, 0);
	HWND lightsPanel = MakePanel(frame, false, 6, 250);
	TEST_ASSERT(sliderPanel != nullptr && lightsPanel != nullptr);

	// Contar barras sozinho escolheria o de luzes e zeraria a iluminacao do
	// usuario em vez dos sliders dele.
	TEST_ASSERT(PickSliderHost(frame, nullptr) == sliderPanel);

	DestroyWindow(frame);
	return true;
}

TEST(PicksTheFullestWhenNobodyScrolls) {
	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);

	HWND few = MakePanel(frame, false, 3, 0);
	HWND many = MakePanel(frame, false, 9, 250);
	TEST_ASSERT(few != nullptr && many != nullptr);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == many);

	DestroyWindow(frame);
	return true;
}

TEST(KeepsThePosePanelOutOfIt) {
	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);

	// O painel de pose tem sete barras e nao rola. Sem exclui-lo, um projeto
	// com poucos sliders faria a tecla apagar a pose em vez dos sliders.
	HWND pose = MakePanel(frame, false, 7, 0);
	HWND sliders = MakePanel(frame, false, 4, 250);
	TEST_ASSERT(pose != nullptr && sliders != nullptr);

	TEST_ASSERT(PickSliderHost(frame, nullptr) == pose); // sem exclusao, escolhe errado
	TEST_ASSERT(PickSliderHost(frame, pose) == sliders); // com exclusao, acerta

	DestroyWindow(frame);
	return true;
}

TEST(IgnoresLooseAndHiddenSliders) {
	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);

	// Uma barra sozinha e um controle avulso -- o sliderClippingStrength do
	// BodySlide, o fovSlider do Outfit Studio -- e nao um painel de sliders.
	MakePanel(frame, false, 1, 0);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == nullptr);

	// Painel recolhido: as barras existem mas ninguem as ve, e zerar o que
	// esta escondido e mexer no que o usuario nao pediu.
	HWND hidden = MakePanel(frame, true, 8, 250);
	TEST_ASSERT(hidden != nullptr);
	ShowWindow(hidden, SW_HIDE);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == nullptr);

	ShowWindow(hidden, SW_SHOW);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == hidden);

	DestroyWindow(frame);
	return true;
}

TEST(FindsSlidersThatReportThemselvesInvisible) {
	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);

	// O caso real, e o que derrubou a primeira versao: no Outfit Studio as
	// barras do painel de sliders respondem IsWindowVisible = falso mesmo
	// desenhadas na tela. O dump mostrou UMA visivel entre quarenta e quatro,
	// e era a de Field of View, que nem e do painel.
	//
	// Por isso a pergunta e feita ao PAINEL, nao a barra.
	HWND panel = MakePanel(frame, true, 0, 0);
	TEST_ASSERT(panel != nullptr);
	for (int i = 0; i < 5; ++i) {
		CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD, // sem WS_VISIBLE
						0, i * 25, 150, 20, panel, nullptr,
						GetModuleHandleW(nullptr), nullptr);
	}

	TEST_ASSERT(PickSliderHost(frame, nullptr) == panel);

	// E a regra antiga continua valendo pelo que ela queria resolver: painel
	// escondido -- que e o painel recolhido do programa -- sai da conta.
	ShowWindow(panel, SW_HIDE);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == nullptr);

	DestroyWindow(frame);
	return true;
}

TEST(FindsTheHostWhenEachSliderSitsInItsOwnRowPanel) {
	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);

	// A forma real do Outfit Studio, e a que derrubou duas versoes seguidas:
	// cada linha de slider e um painel proprio, com o lapis, a caixa, o nome, a
	// barra e a porcentagem dentro. Agrupando por pai DIRETO davam cento e
	// trinta e um paineis de uma barra cada, e nenhum passava na regra de ter
	// pelo menos duas.
	HWND scroll = MakePanel(frame, true, 0, 0);
	TEST_ASSERT(scroll != nullptr);

	for (int i = 0; i < 6; ++i) {
		HWND rowPanel = CreateWindowExW(0, L"BSOSZeroHost", L"", WS_CHILD | WS_VISIBLE,
										0, i * 25, 200, 20, scroll, nullptr,
										GetModuleHandleW(nullptr), nullptr);
		TEST_ASSERT(rowPanel != nullptr);
		CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE,
						40, 0, 150, 20, rowPanel, nullptr,
						GetModuleHandleW(nullptr), nullptr);
	}

	// Sobe um nivel e acha a area que rola, que e o pai das linhas.
	TEST_ASSERT(PickSliderHost(frame, nullptr) == scroll);

	// E o nivel 1 continua valendo para o BodySlide, onde as barras sao filhas
	// diretas da area que rola.
	HWND flat = MakePanel(frame, true, 4, 400);
	TEST_ASSERT(flat != nullptr);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == flat); // nivel 1 vence, e vem antes

	DestroyWindow(frame);
	return true;
}

TEST(SurvivesAFrameWithoutSliders) {
	TEST_ASSERT(PickSliderHost(nullptr, nullptr) == nullptr);

	HWND frame = MakeFrame();
	TEST_ASSERT(frame != nullptr);
	TEST_ASSERT(PickSliderHost(frame, nullptr) == nullptr);
	DestroyWindow(frame);
	return true;
}
