// Matematica do arrasto que ajusta o brush.
#include "test_util.h"

#include "features/brush_resize.h"

TEST(BrushStepMathIsIncremental) {
	// O ponto: os passos sao incrementais. O modo guarda quantos ja aplicou,
	// entao arrastar de volta encolhe em vez de continuar crescendo.
	TEST_ASSERT(StepsToApply(10, 1.0f, 0, 300) == 10);
	TEST_ASSERT(StepsToApply(10, 1.0f, 10, 300) == 0);  // mesmo lugar, nada a fazer
	TEST_ASSERT(StepsToApply(25, 1.0f, 10, 300) == 15); // seguiu para a direita
	TEST_ASSERT(StepsToApply(5, 1.0f, 10, 300) == -5);  // voltou um pouco
	TEST_ASSERT(StepsToApply(0, 1.0f, 10, 300) == -10); // voltou ao inicio
	TEST_ASSERT(StepsToApply(-8, 1.0f, 0, 300) == -8);  // passou para a esquerda

	// Sensibilidade escala o deslocamento.
	TEST_ASSERT(StepsToApply(10, 2.0f, 0, 300) == 20);
	TEST_ASSERT(StepsToApply(10, 0.5f, 0, 300) == 5);

	// Sensibilidade invalida nao pode gerar passo nenhum.
	TEST_ASSERT(StepsToApply(10, 0.0f, 0, 300) == 0);
	TEST_ASSERT(StepsToApply(10, -1.0f, 0, 300) == 0);

	// O range do tamanho sao 300 passos. Arrastar a tela inteira nao pode
	// acumular milhares de comandos, senao o cancelar devolveria o brush para
	// longe do tamanho original.
	TEST_ASSERT(StepsToApply(5000, 1.0f, 0, 300) == 300);
	TEST_ASSERT(StepsToApply(-5000, 1.0f, 0, 300) == -300);
	return true;
}

TEST(TheCeilingFollowsTheQuantityBeingChanged) {
	// A forca tem menos passos que o tamanho, e o teto e por grandeza.
	//
	// Isso importa mais do que parece: os comandos que passam do fim da faixa
	// sao ignorados pelo Outfit Studio, mas continuariam contando aqui. No
	// cancelar, a conta aplicaria de volta passos que nunca surtiram efeito, e
	// o brush terminaria mais fraco do que comecou.
	TEST_ASSERT(StepsToApply(5000, 1.0f, 0, 100) == 100);
	TEST_ASSERT(StepsToApply(-5000, 1.0f, 0, 100) == -100);

	// Abaixo do teto, nada muda entre uma grandeza e outra.
	TEST_ASSERT(StepsToApply(40, 1.0f, 0, 100) == 40);
	TEST_ASSERT(StepsToApply(40, 1.0f, 0, 300) == 40);

	// E o incremento continua valendo depois de saturar: voltar o mouse tem
	// que desfazer a partir do teto, nao do valor cru do deslocamento.
	TEST_ASSERT(StepsToApply(5000, 1.0f, 100, 100) == 0);
	TEST_ASSERT(StepsToApply(60, 1.0f, 100, 100) == -40);

	// Teto invalido nao pode virar um arrasto que mexe ao contrario.
	TEST_ASSERT(StepsToApply(50, 1.0f, 0, 0) == 0);
	TEST_ASSERT(StepsToApply(50, 1.0f, 0, -10) == 0);
	return true;
}
