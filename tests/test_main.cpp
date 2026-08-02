#include "test_util.h"

// SEH em funcao separada: nao pode coexistir com objetos C++ que precisam unwind.
static bool RunGuarded(bool (*fn)()) {
	__try {
		return fn();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		std::printf("\n      EXCECAO durante o teste\n");
		return false;
	}
}

int main() {
	std::printf("\n  BSOSImprovements -- testes\n");
	std::printf("  ----------------------------------------------------------\n");

	int pass = 0, fail = 0;
	for (const TestCase& t : TestRegistry()) {
		std::printf("  %-48s", t.name);
		if (RunGuarded(t.fn)) {
			std::printf(" OK\n");
			++pass;
		} else {
			std::printf("      ^ FALHOU\n");
			++fail;
		}
	}

	std::printf("  ----------------------------------------------------------\n");
	std::printf("  %d passou, %d falhou\n\n", pass, fail);
	return fail ? 1 : 0;
}
