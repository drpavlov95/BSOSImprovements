// Deteccao do fim de um ciclo de repopulacao da arvore de shapes.
#include "test_util.h"

#include "features/ref_autoselect.h"

TEST(DetectsPopulationCycle) {
	PopulationTracker tracker;

	// Primeiro carregamento da sessao: nao ha nada para deletar, so inserts e
	// o expand do fim. Este e o caso mais comum, e um criterio que exigisse
	// deleces nunca o reconheceria -- as deleces so acontecem quando ja havia
	// um projeto aberto.
	tracker.OnInsert();
	tracker.OnInsert();
	tracker.OnInsert();
	tracker.OnExpand();
	TEST_ASSERT(tracker.CycleFinished());

	// Expand solto, sem insercao: o usuario abriu um no da arvore na mao.
	tracker.Reset();
	tracker.OnExpand();
	TEST_ASSERT(!tracker.CycleFinished());

	// Insercoes ainda em andamento, sem o expand que fecha o ciclo.
	tracker.Reset();
	tracker.OnInsert();
	tracker.OnInsert();
	TEST_ASSERT(!tracker.CycleFinished());

	// O chamador zera a cada expand, tenha disparado ou nao. Sem isso as
	// insercoes de uma operacao qualquer ficariam acumuladas, e um expand
	// avulso muito depois -- o usuario abrindo um no -- fecharia um ciclo que
	// nunca existiu. Esta e a sequencia que aquele bug produzia:
	tracker.Reset();
	tracker.OnInsert(); // alguma operacao inseriu um item
	tracker.OnExpand(); // ciclo legitimo, dispara
	TEST_ASSERT(tracker.CycleFinished());
	tracker.Reset();    // o chamador zera aqui
	tracker.OnExpand(); // usuario abre um no, muito depois
	TEST_ASSERT(!tracker.CycleFinished());
	return true;
}
