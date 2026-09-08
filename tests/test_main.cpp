#include "test_util.h"

#include <cstring>

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
		std::printf("\n      EXCECAO durante o teste (codigo 0x%08lX)\n",
					GetExceptionCode());
		return false;
	}
}

// Nem toda queda cai dentro do __except acima: corrupcao de heap costuma
// estourar depois, ja fora do teste que a causou. Sem este filtro o processo
// morre calado e o unico dado disponivel e o codigo de saida.
static LONG WINAPI ReportCrash(EXCEPTION_POINTERS* info) {
	std::printf("\n\n  QUEDA fora do teste: codigo 0x%08lX em %p\n",
				info->ExceptionRecord->ExceptionCode,
				info->ExceptionRecord->ExceptionAddress);
	std::fflush(stdout);
	return EXCEPTION_EXECUTE_HANDLER;
}

// Um argumento opcional filtra por substring do nome. Serve para isolar um
// teste quando ele derruba o processo: nesse caso o runner nao chega ao fim, e
// rodar so o suspeito e a unica forma de olhar para ele sozinho.
int main(int argc, char** argv) {
	const char* filter = (argc > 1) ? argv[1] : nullptr;
	SetUnhandledExceptionFilter(ReportCrash);

	std::printf("\n  BSOSImprovements -- testes\n");
	std::printf("  ----------------------------------------------------------\n");

	int pass = 0, fail = 0;
	for (const TestCase& t : TestRegistry()) {
		if (filter && !std::strstr(t.name, filter))
			continue;
		std::printf("  %-48s", t.name);
		// Descarrega ANTES de rodar. Estes testes criam janelas e mandam
		// mensagens de verdade, entao um deles pode derrubar o processo -- e
		// com a saida em buffer, uma queda leva junto tudo o que ja tinha sido
		// impresso, inclusive o nome do teste que caiu.
		std::fflush(stdout);
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
