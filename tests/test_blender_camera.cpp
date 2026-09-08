// A traducao do esquema do Blender para o do Outfit Studio.
#include "test_util.h"

#include "features/blender_camera.h"

namespace {

// Roda uma mensagem pela traducao e devolve o que sairia dela.
struct Translated {
	bool changed = false;
	UINT message = 0;
	WPARAM wParam = 0;
};

Translated Run(CameraState& state, UINT message, WPARAM wParam) {
	Translated out;
	out.message = message;
	out.wParam = wParam;
	out.changed = TranslateCameraMessage(state, out.message, out.wParam);
	return out;
}

} // namespace

TEST(MiddleDragOrbitsLikeTheRightButton) {
	CameraState state;

	// Apertar o botao do meio sem Shift chega no programa como botao direito,
	// que e quem orbita no Outfit Studio.
	Translated down = Run(state, WM_MBUTTONDOWN, MK_MBUTTON);
	TEST_ASSERT(down.changed);
	TEST_ASSERT(down.message == WM_RBUTTONDOWN);
	TEST_ASSERT((down.wParam & MK_RBUTTON) != 0);
	TEST_ASSERT((down.wParam & MK_MBUTTON) == 0); // senao seriam dois botoes
	TEST_ASSERT(state.orbiting);

	// O movimento durante o arrasto tambem: o programa pode consultar a
	// mascara em vez de lembrar de qual botao foi apertado.
	Translated move = Run(state, WM_MOUSEMOVE, MK_MBUTTON);
	TEST_ASSERT(move.changed);
	TEST_ASSERT(move.message == WM_MOUSEMOVE);
	TEST_ASSERT((move.wParam & MK_RBUTTON) != 0);
	TEST_ASSERT((move.wParam & MK_MBUTTON) == 0);

	// E soltar fecha o arrasto pelo mesmo botao com que ele comecou. Soltar
	// como botao do meio deixaria o programa orbitando para sempre.
	Translated up = Run(state, WM_MBUTTONUP, 0);
	TEST_ASSERT(up.changed);
	TEST_ASSERT(up.message == WM_RBUTTONUP);
	TEST_ASSERT(!state.orbiting);

	// Terminado o arrasto, o movimento volta a passar intacto.
	TEST_ASSERT(!Run(state, WM_MOUSEMOVE, 0).changed);
	return true;
}

TEST(ShiftMiddleStaysPan) {
	CameraState state;

	// Shift+meio ja e o pan do Outfit Studio: o botao nao muda, so o Shift sai.
	Translated down = Run(state, WM_MBUTTONDOWN, MK_MBUTTON | MK_SHIFT);
	TEST_ASSERT(down.changed);
	TEST_ASSERT(down.message == WM_MBUTTONDOWN);
	TEST_ASSERT((down.wParam & MK_MBUTTON) != 0);
	TEST_ASSERT((down.wParam & MK_SHIFT) == 0);
	TEST_ASSERT(state.panning);
	TEST_ASSERT(!state.orbiting);

	Translated move = Run(state, WM_MOUSEMOVE, MK_MBUTTON | MK_SHIFT);
	TEST_ASSERT((move.wParam & MK_MBUTTON) != 0);
	TEST_ASSERT((move.wParam & MK_SHIFT) == 0);

	Translated up = Run(state, WM_MBUTTONUP, MK_SHIFT);
	TEST_ASSERT(up.message == WM_MBUTTONUP);
	TEST_ASSERT(!state.panning);
	return true;
}

TEST(DragModeIsDecidedAtThePress) {
	CameraState state;

	// Comeca orbitando sem Shift.
	Run(state, WM_MBUTTONDOWN, MK_MBUTTON);
	TEST_ASSERT(state.orbiting);

	// Apertar Shift no meio do caminho nao vira pan. E o comportamento do
	// Blender, e tambem o unico coerente: o programa ja esta com um arrasto de
	// botao direito aberto, e trocar de botao no meio o deixaria pendurado.
	Translated move = Run(state, WM_MOUSEMOVE, MK_MBUTTON | MK_SHIFT);
	TEST_ASSERT((move.wParam & MK_RBUTTON) != 0);
	TEST_ASSERT(state.orbiting);
	TEST_ASSERT(!state.panning);
	return true;
}

TEST(LeavesEverythingElseAlone) {
	CameraState state;

	// Os outros botoes nao sao da camera.
	TEST_ASSERT(!Run(state, WM_LBUTTONDOWN, MK_LBUTTON).changed);
	TEST_ASSERT(!Run(state, WM_RBUTTONDOWN, MK_RBUTTON).changed);
	TEST_ASSERT(!Run(state, WM_MOUSEWHEEL, 0).changed);
	TEST_ASSERT(!Run(state, WM_KEYDOWN, 'B').changed);

	// Movimento sem arrasto nosso em curso passa intacto, inclusive com Shift
	// apertado -- Shift sozinho e usado pelos brushes.
	TEST_ASSERT(!Run(state, WM_MOUSEMOVE, MK_SHIFT).changed);

	// Soltar o botao do meio sem ter apertado antes nao inventa um evento.
	TEST_ASSERT(!Run(state, WM_MBUTTONUP, 0).changed);
	return true;
}

TEST(NoticesADragThatEndedOutsideTheWindow) {
	CameraState idle;
	CameraState orbiting;
	orbiting.orbiting = true;
	CameraState panning;
	panning.panning = true;

	// O caso que importa: alt-tab no meio do arrasto, o botao e solto sobre
	// outro programa e o WM_MBUTTONUP nunca chega aqui. O unico sinal que
	// sobra e a tecla ja nao estar apertada.
	TEST_ASSERT(DragWasReleasedOutside(orbiting, WM_MOUSEMOVE, false));
	TEST_ASSERT(DragWasReleasedOutside(panning, WM_MOUSEMOVE, false));

	// Arrasto normal em curso: nao encerra nada.
	TEST_ASSERT(!DragWasReleasedOutside(orbiting, WM_MOUSEMOVE, true));

	// Sem arrasto nosso nao ha o que encerrar, com botao apertado ou nao.
	TEST_ASSERT(!DragWasReleasedOutside(idle, WM_MOUSEMOVE, false));
	TEST_ASSERT(!DragWasReleasedOutside(idle, WM_MOUSEMOVE, true));

	// So o movimento serve de gatilho. Logo depois do aperto o estado fisico da
	// tecla pode ainda nao ter chegado aqui, e testar cedo demais cancelaria o
	// arrasto no instante em que ele comeca.
	TEST_ASSERT(!DragWasReleasedOutside(orbiting, WM_MBUTTONDOWN, false));
	TEST_ASSERT(!DragWasReleasedOutside(orbiting, WM_MBUTTONUP, false));
	TEST_ASSERT(!DragWasReleasedOutside(orbiting, WM_KEYDOWN, false));
	return true;
}

TEST(DoubleClickOrbitsToo) {
	CameraState state;

	// O duplo clique vem como uma mensagem propria. Sem traduzi-la, o segundo
	// clique de um arrasto rapido escaparia como botao do meio.
	Translated hit = Run(state, WM_MBUTTONDBLCLK, MK_MBUTTON);
	TEST_ASSERT(hit.changed);
	TEST_ASSERT(hit.message == WM_RBUTTONDBLCLK);
	TEST_ASSERT(state.orbiting);
	return true;
}
