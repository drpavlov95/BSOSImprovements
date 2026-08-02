// Matematica do arrasto de redimensionar brush.
#include "test_util.h"

#include "features/brush_resize.h"

TEST(BrushStepMathIsIncremental) {
	// O ponto: os passos sao incrementais. O modo guarda quantos ja aplicou,
	// entao arrastar de volta encolhe em vez de continuar crescendo.
	TEST_ASSERT(StepsToApply(10, 1.0f, 0) == 10);
	TEST_ASSERT(StepsToApply(10, 1.0f, 10) == 0);  // mesmo lugar, nada a fazer
	TEST_ASSERT(StepsToApply(25, 1.0f, 10) == 15); // seguiu para a direita
	TEST_ASSERT(StepsToApply(5, 1.0f, 10) == -5);  // voltou um pouco
	TEST_ASSERT(StepsToApply(0, 1.0f, 10) == -10); // voltou ao inicio
	TEST_ASSERT(StepsToApply(-8, 1.0f, 0) == -8);  // passou para a esquerda

	// Sensibilidade escala o deslocamento.
	TEST_ASSERT(StepsToApply(10, 2.0f, 0) == 20);
	TEST_ASSERT(StepsToApply(10, 0.5f, 0) == 5);

	// Sensibilidade invalida nao pode gerar passo nenhum.
	TEST_ASSERT(StepsToApply(10, 0.0f, 0) == 0);
	TEST_ASSERT(StepsToApply(10, -1.0f, 0) == 0);

	// O range do brush sao 300 passos. Arrastar a tela inteira nao pode
	// acumular milhares de comandos, senao o cancelar devolveria o brush para
	// longe do tamanho original.
	TEST_ASSERT(StepsToApply(5000, 1.0f, 0) == 300);
	TEST_ASSERT(StepsToApply(-5000, 1.0f, 0) == -300);
	return true;
}
