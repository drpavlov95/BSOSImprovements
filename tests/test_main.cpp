#include "test_util.h"

// Puxa o comctl32 v6, como qualquer app com visual styles -- e o BodySlide,
// sendo wxWidgets, e um deles. Sem isto o processo de teste recebe a v5, onde
// mensagens como EM_SETCUEBANNER simplesmente nao existem, e o teste mediria um
// ambiente que nao e o de producao.
#pragma comment(linker,                                                        \
				"/manifestdependency:\"type='win32' "                          \
				"name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "   \
				"processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "  \
				"language='*'\"")

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
