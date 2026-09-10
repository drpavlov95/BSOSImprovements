// A aritmetica do arrasto que reordena sliders.
#include "test_util.h"

#include "features/slider_reorder.h"

#include <algorithm>

TEST(FindsTheSlotUnderThePointer) {
	// Lugares de 25 em 25, como as linhas do painel de sliders.
	const std::vector<int> slots = {0, 25, 50, 75, 100};

	// Dentro de um lugar, ele mesmo.
	TEST_ASSERT(SlotAt(slots, 0) == 0);
	TEST_ASSERT(SlotAt(slots, 51) == 2);

	// A troca acontece no MEIO entre dois lugares, e nao ao encostar. Trocar
	// cedo demais faria a lista tremer com o mouse quase parado.
	TEST_ASSERT(SlotAt(slots, 36) == 1); // o meio de 25->50 e 37: ainda no 1
	TEST_ASSERT(SlotAt(slots, 38) == 2); // passou do meio
	TEST_ASSERT(SlotAt(slots, 11) == 0); // o meio de 0->25 e 12: ainda no 0
	TEST_ASSERT(SlotAt(slots, 12) == 1); // no meio exato, ja conta como o proximo

	// Fora da lista cai nas pontas: arrastar para cima do topo quer dizer
	// "poe no comeco", e nao "nao faz nada".
	TEST_ASSERT(SlotAt(slots, -500) == 0);
	TEST_ASSERT(SlotAt(slots, 5000) == 4);

	// Lista vazia nao tem lugar nenhum.
	TEST_ASSERT(SlotAt({}, 10) == -1);

	// Uma linha so: sempre ela.
	TEST_ASSERT(SlotAt({7}, -100) == 0);
	TEST_ASSERT(SlotAt({7}, 900) == 0);
	return true;
}

TEST(SlotSpacingIsReadFromTheListNotAssumed) {
	// As linhas do Outfit Studio nao tem todas a mesma altura -- uma categoria
	// recolhida, um separador, e o espacamento muda. Supor altura fixa faria a
	// linha arrastada pular de lugar antes da hora.
	const std::vector<int> uneven = {0, 10, 60, 200};

	TEST_ASSERT(SlotAt(uneven, 4) == 0);   // meio de 0->10 e 5
	TEST_ASSERT(SlotAt(uneven, 6) == 1);
	TEST_ASSERT(SlotAt(uneven, 34) == 1);  // meio de 10->60 e 35
	TEST_ASSERT(SlotAt(uneven, 36) == 2);
	TEST_ASSERT(SlotAt(uneven, 129) == 2); // meio de 60->200 e 130
	TEST_ASSERT(SlotAt(uneven, 131) == 3);
	return true;
}

namespace {

std::vector<HWND> Fake(int count) {
	std::vector<HWND> out;
	for (int i = 1; i <= count; ++i)
		out.push_back(reinterpret_cast<HWND>(static_cast<UINT_PTR>(i)));
	return out;
}

bool Is(const std::vector<HWND>& order, std::initializer_list<int> expected) {
	if (order.size() != expected.size())
		return false;
	size_t i = 0;
	for (int want : expected) {
		if (order[i++] != reinterpret_cast<HWND>(static_cast<UINT_PTR>(want)))
			return false;
	}
	return true;
}

} // namespace

TEST(MovingARowPushesTheOthersInsteadOfSwapping) {
	std::vector<HWND> order = Fake(5); // 1 2 3 4 5

	// Levar a primeira para o fim empurra as outras um lugar para cima. Numa
	// TROCA, a ultima saltaria para o topo -- que nao e o que alguem espera ao
	// arrastar uma linha lista abaixo.
	MoveInOrder(order, 0, 4);
	TEST_ASSERT(Is(order, {2, 3, 4, 5, 1}));

	// E de volta.
	MoveInOrder(order, 4, 0);
	TEST_ASSERT(Is(order, {1, 2, 3, 4, 5}));

	// Um passo para baixo troca com o vizinho, que e o caso comum.
	MoveInOrder(order, 1, 2);
	TEST_ASSERT(Is(order, {1, 3, 2, 4, 5}));

	// Para o mesmo lugar nao mexe.
	MoveInOrder(order, 2, 2);
	TEST_ASSERT(Is(order, {1, 3, 2, 4, 5}));

	// Indices fora da lista nao podem corromper nada.
	MoveInOrder(order, -1, 2);
	MoveInOrder(order, 0, 99);
	MoveInOrder(order, 99, 0);
	TEST_ASSERT(Is(order, {1, 3, 2, 4, 5}));

	std::vector<HWND> empty;
	MoveInOrder(empty, 0, 0);
	TEST_ASSERT(empty.empty());
	return true;
}

namespace {

std::vector<int> Order(std::initializer_list<const wchar_t*> desired,
					   std::initializer_list<const wchar_t*> present) {
	return ApplyDesiredOrder(std::vector<std::wstring>(desired.begin(), desired.end()),
							 std::vector<std::wstring>(present.begin(), present.end()));
}

bool Same(const std::vector<int>& got, std::initializer_list<int> expected) {
	return got.size() == expected.size() && std::equal(got.begin(), got.end(), expected.begin());
}

} // namespace

TEST(TheChosenOrderSurvivesTheListBeingRebuilt) {
	// O usuario deixou Weight antes de Belly. O programa refaz a lista e devolve
	// tudo na ordem original: a escolha tem que valer de novo.
	TEST_ASSERT(Same(Order({L"Weight", L"Belly", L"Chubby"}, {L"Belly", L"Chubby", L"Weight"}),
					 {2, 0, 1}));

	// Sem escolha nenhuma, a ordem do programa fica como esta.
	TEST_ASSERT(Same(Order({}, {L"Belly", L"Chubby"}), {0, 1}));
	return true;
}

TEST(SlidersOutsideTheChosenOrderGoToTheEndInsteadOfDisappearing) {
	// Trocar de outfit traz sliders que o usuario nunca ordenou. Eles vao para o
	// fim, na ordem em que o programa os deu -- descarta-los sumiria com slider
	// da tela, que e a unica falha realmente grave que esta funcao pode ter.
	TEST_ASSERT(Same(Order({L"Weight", L"Belly"}, {L"Belly", L"Nova", L"Weight", L"Outra"}),
					 {2, 0, 1, 3}));

	// Nome que o usuario ordenou e que este outfit nao tem simplesmente nao
	// entra, e nao desloca ninguem.
	TEST_ASSERT(Same(Order({L"Sumiu", L"Weight", L"Foi"}, {L"Belly", L"Weight"}), {1, 0}));

	// Todo mundo novo: nada a reordenar.
	TEST_ASSERT(Same(Order({L"Weight"}, {L"A", L"B"}), {0, 1}));
	return true;
}

TEST(ARepeatedNameConsumesOneSliderAtATime) {
	// Dois sliders com o mesmo nome nao podem virar o mesmo indice duas vezes:
	// o resultado deixaria de ser uma permutacao e uma das linhas ficaria em
	// cima da outra.
	const std::vector<int> got = Order({L"Igual", L"Igual", L"Fim"}, {L"Fim", L"Igual", L"Igual"});
	TEST_ASSERT(Same(got, {1, 2, 0}));

	// Toda saida e uma permutacao completa da entrada, sempre.
	std::vector<int> sorted = got;
	std::sort(sorted.begin(), sorted.end());
	TEST_ASSERT(Same(sorted, {0, 1, 2}));
	return true;
}
