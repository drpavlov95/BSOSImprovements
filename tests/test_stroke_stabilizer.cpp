// A matematica da corda que segura o pincel.
#include "test_util.h"

#include <cmath>

#include "features/stroke_stabilizer.h"

namespace {

POINT At(LONG x, LONG y) {
	POINT p;
	p.x = x;
	p.y = y;
	return p;
}

bool Is(POINT p, LONG x, LONG y) {
	return p.x == x && p.y == y;
}

double DistanceBetween(POINT a, POINT b) {
	const double dx = static_cast<double>(a.x) - b.x;
	const double dy = static_cast<double>(a.y) - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

} // namespace

TEST(SmallMovementDoesNotMoveTheBrush) {
	// E este o teste que descreve a feature inteira: tremor e movimento
	// pequeno, e movimento pequeno nao estica a corda, entao o pincel nao anda.
	const POINT brush = At(100, 100);

	TEST_ASSERT(Is(StabilizeStroke(brush, At(103, 100), 20), 100, 100));
	TEST_ASSERT(Is(StabilizeStroke(brush, At(100, 112), 20), 100, 100));
	TEST_ASSERT(Is(StabilizeStroke(brush, At(88, 92), 20), 100, 100));

	// Exatamente no raio ainda nao puxa: a corda esticou, mas nao arrastou.
	TEST_ASSERT(Is(StabilizeStroke(brush, At(120, 100), 20), 100, 100));
	return true;
}

TEST(TheBrushFollowsOnceTheRopeIsTaut) {
	const POINT brush = At(100, 100);

	// Cursor a 50 px com corda de 20: o pincel anda 30 e fica a 20 de
	// distancia.
	const POINT moved = StabilizeStroke(brush, At(150, 100), 20);
	TEST_ASSERT(Is(moved, 130, 100));

	// A propriedade que importa nao e o numero, e a distancia final: depois de
	// um movimento grande o pincel fica SEMPRE a exatamente um raio do cursor.
	const POINT cursor = At(400, 380);
	const POINT dragged = StabilizeStroke(brush, cursor, 25);
	TEST_ASSERT(std::fabs(DistanceBetween(dragged, cursor) - 25.0) < 1.0);

	// E ele anda na direcao do cursor, nunca para o lado.
	TEST_ASSERT(dragged.x > brush.x && dragged.y > brush.y);
	return true;
}

TEST(ARealDragConvergesToAConstantLag) {
	// Um arrasto reto: o pincel comeca junto, fica para tras enquanto a corda
	// estica, e depois acompanha com atraso constante. Sem essa estabilizacao
	// o atraso seria zero e o tremor passaria inteiro.
	const int radius = 15;
	POINT brush = At(0, 0);

	for (int step = 1; step <= 40; ++step)
		brush = StabilizeStroke(brush, At(step * 5, 0), radius);

	const POINT cursor = At(200, 0);
	TEST_ASSERT(std::fabs(DistanceBetween(brush, cursor) - radius) < 1.0);

	// Parar a mao nao faz o pincel alcancar o cursor: a corda continua
	// esticada. E por isso que o traco termina onde o pincel esta, e nao onde o
	// cursor parou.
	const POINT resting = StabilizeStroke(brush, cursor, radius);
	TEST_ASSERT(Is(resting, brush.x, brush.y));
	return true;
}

TEST(ZeroRadiusMeansNoStabilizing) {
	const POINT brush = At(100, 100);
	const POINT cursor = At(137, 152);

	// Raio zero e como o INI desliga a feature, entao ele tem que devolver o
	// cursor cru -- nao o pincel parado, que congelaria a pincelada.
	TEST_ASSERT(Is(StabilizeStroke(brush, cursor, 0), 137, 152));
	TEST_ASSERT(Is(StabilizeStroke(brush, cursor, -5), 137, 152));
	return true;
}

TEST(BrushAlreadyOnTheCursorStaysPut) {
	// Sem movimento nao ha divisao por distancia zero.
	const POINT same = At(50, 50);
	TEST_ASSERT(Is(StabilizeStroke(same, same, 20), 50, 50));
	TEST_ASSERT(Is(StabilizeStroke(same, same, 1), 50, 50));
	return true;
}

TEST(WorksTheSameInEveryDirection) {
	// A corda e um circulo, nao um quadrado: um movimento na diagonal tem que
	// ser tratado pela distancia real, senao o estabilizador seguraria mais na
	// diagonal do que na horizontal.
	const POINT brush = At(0, 0);
	const int radius = 10;

	// 30 na horizontal e 30 na diagonal andam distancias diferentes em x, mas
	// as duas terminam a um raio do cursor.
	const POINT straight = StabilizeStroke(brush, At(30, 0), radius);
	TEST_ASSERT(std::fabs(DistanceBetween(straight, At(30, 0)) - radius) < 1.0);

	const POINT diagonal = StabilizeStroke(brush, At(30, 30), radius);
	TEST_ASSERT(std::fabs(DistanceBetween(diagonal, At(30, 30)) - radius) < 1.0);

	// E na diagonal ele nao anda 30 em cada eixo -- isso seria tratar a corda
	// como um quadrado.
	TEST_ASSERT(diagonal.x < 30 && diagonal.y < 30);
	TEST_ASSERT(diagonal.x == diagonal.y); // simetrico nos dois eixos
	return true;
}
