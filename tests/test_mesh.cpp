// Leitura e escrita de OBJ, e o pareamento de vertices espelhados.
#include "test_util.h"

#include <cmath>
#include <string>

#include "mesh/mirror.h"
#include "mesh/obj.h"

namespace {

bool Near(float a, float b) {
	return std::fabs(a - b) < 0.0005f;
}

} // namespace

TEST(ReadsPositionsAndTrianglesFromObj) {
	ObjMesh mesh;
	TEST_ASSERT(ParseObj("# um comentario\n"
						 "v 1.0 2.0 3.0\n"
						 "vt 0.5 0.5\n"
						 "vn 0.0 1.0 0.0\n"
						 "v -1.0 2.0 3.0\n"
						 "v 0.0 5.0 3.0\n"
						 "f 1 2 3\n",
						 mesh));

	TEST_ASSERT(mesh.VertexCount() == 3);
	TEST_ASSERT(mesh.TriangleCount() == 1);

	// A ordem dos vertices e o contrato inteiro: a mascara do Outfit Studio e
	// indexada por posicao na lista, entao reordenar aqui a devolveria aplicada
	// nos vertices errados.
	TEST_ASSERT(Near(mesh.positions[0].x, 1.0f));
	TEST_ASSERT(Near(mesh.positions[1].x, -1.0f));
	TEST_ASSERT(Near(mesh.positions[2].y, 5.0f));

	// OBJ conta de um; aqui dentro tudo conta de zero.
	TEST_ASSERT(mesh.triangles[0] == 0);
	TEST_ASSERT(mesh.triangles[1] == 1);
	TEST_ASSERT(mesh.triangles[2] == 2);
	return true;
}

TEST(HandlesTheObjDialectsThatShowUp) {
	// Face com barras: "v/vt/vn" e "v//vn". So o primeiro numero interessa, e
	// tratar a barra como separador de numero leria o indice de UV como se
	// fosse de vertice.
	ObjMesh slashes;
	TEST_ASSERT(ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\n"
						 "f 1/1/1 2/2/2 3/3/3\n",
						 slashes));
	TEST_ASSERT(slashes.TriangleCount() == 1);
	TEST_ASSERT(slashes.triangles[1] == 1);

	ObjMesh doubleSlash;
	TEST_ASSERT(ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1//1 2//2 3//3\n", doubleSlash));
	TEST_ASSERT(doubleSlash.TriangleCount() == 1);

	// Quad vira dois triangulos. O Outfit Studio exporta triangulos, mas OBJ
	// que passou por outro programa no caminho costuma trazer quads.
	ObjMesh quad;
	TEST_ASSERT(ParseObj("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n", quad));
	TEST_ASSERT(quad.TriangleCount() == 2);

	// Indice negativo conta de tras para frente a partir do ultimo vertice
	// lido. E OBJ valido, e ignorar isso apontaria a face para o vertice
	// errado sem erro nenhum.
	ObjMesh relative;
	TEST_ASSERT(ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n", relative));
	TEST_ASSERT(relative.TriangleCount() == 1);
	TEST_ASSERT(relative.triangles[0] == 0);
	TEST_ASSERT(relative.triangles[2] == 2);

	// CRLF, que e o que sai de um programa do Windows.
	ObjMesh crlf;
	TEST_ASSERT(ParseObj("v 1 2 3\r\nv 4 5 6\r\nv 7 8 9\r\nf 1 2 3\r\n", crlf));
	TEST_ASSERT(crlf.VertexCount() == 3);
	TEST_ASSERT(Near(crlf.positions[1].y, 5.0f));

	// Ultima linha sem quebra no fim.
	ObjMesh noTrailing;
	TEST_ASSERT(ParseObj("v 1 2 3", noTrailing));
	TEST_ASSERT(noTrailing.VertexCount() == 1);
	TEST_ASSERT(Near(noTrailing.positions[0].z, 3.0f));
	return true;
}

TEST(RefusesBrokenObjInsteadOfGuessing) {
	ObjMesh mesh;

	// Sem vertice nao ha malha.
	TEST_ASSERT(!ParseObj("", mesh));
	TEST_ASSERT(!ParseObj("# so comentario\n", mesh));
	TEST_ASSERT(!ParseObj("f 1 2 3\n", mesh));

	// Linha de vertice incompleta derruba a leitura inteira, de proposito.
	// Descartar so ela deslocaria TODOS os indices depois dela, e a mascara
	// voltaria aplicada nos vertices errados -- um estrago silencioso.
	TEST_ASSERT(!ParseObj("v 1 2 3\nv 4 5\nv 7 8 9\n", mesh));
	TEST_ASSERT(!ParseObj("v 1 2 3\nv a b c\n", mesh));

	// Face que aponta para fora da lista e ignorada, mas os vertices ficam:
	// aqui nada se desloca, entao nao ha risco de trocar indice.
	ObjMesh outOfRange;
	TEST_ASSERT(ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n", outOfRange));
	TEST_ASSERT(outOfRange.VertexCount() == 3);
	TEST_ASSERT(outOfRange.TriangleCount() == 0);
	return true;
}

TEST(ObjSurvivesTheRoundTrip) {
	// O ida-e-volta e a operacao inteira desta arquitetura: exportar, mexer,
	// reimportar. Se a escrita nao reproduzir o que a leitura entendeu, toda
	// feature construida em cima disso move a malha sem querer.
	const std::string original =
		"v 1.500000 -2.250000 0.125000\n"
		"v -1.500000 -2.250000 0.125000\n"
		"v 0.000000 3.000000 -4.750000\n"
		"f 1 2 3\n";

	ObjMesh first;
	TEST_ASSERT(ParseObj(original, first));

	const std::string written = WriteObj(first);
	ObjMesh second;
	TEST_ASSERT(ParseObj(written, second));

	TEST_ASSERT(second.VertexCount() == first.VertexCount());
	TEST_ASSERT(second.TriangleCount() == first.TriangleCount());
	for (size_t i = 0; i < first.VertexCount(); ++i) {
		TEST_ASSERT(Near(first.positions[i].x, second.positions[i].x));
		TEST_ASSERT(Near(first.positions[i].y, second.positions[i].y));
		TEST_ASSERT(Near(first.positions[i].z, second.positions[i].z));
	}
	for (size_t i = 0; i < first.triangles.size(); ++i)
		TEST_ASSERT(first.triangles[i] == second.triangles[i]);

	// E o texto escrito tem que ser estavel: escrever duas vezes da o mesmo
	// arquivo, senao cada ida-e-volta iria empurrando o valor.
	TEST_ASSERT(WriteObj(second) == written);
	return true;
}

namespace {

// Um corpo de mentira, simetrico no eixo X: pares em volta do centro e alguns
// vertices exatamente no plano central.
std::vector<Vec3> SymmetricBody() {
	return {
		{2.0f, 10.0f, 0.0f},  // 0  <-> 1
		{-2.0f, 10.0f, 0.0f}, // 1
		{5.0f, 20.0f, 1.0f},  // 2  <-> 3
		{-5.0f, 20.0f, 1.0f}, // 3
		{0.0f, 30.0f, 2.0f},  // 4  no centro
		{0.0f, 40.0f, 3.0f},  // 5  no centro
	};
}

} // namespace

TEST(PairsVerticesAcrossTheCenterPlane) {
	const std::vector<Vec3> body = SymmetricBody();
	const std::vector<int> pairs = PairMirroredVertices(body, 0.01f);

	TEST_ASSERT(pairs.size() == body.size());

	// Os pares se apontam mutuamente.
	TEST_ASSERT(pairs[0] == 1 && pairs[1] == 0);
	TEST_ASSERT(pairs[2] == 3 && pairs[3] == 2);

	// Vertice no plano central e o proprio espelho. Nao e um caso de erro: e a
	// costura do meio do corpo, e ela tem que continuar mascarada.
	TEST_ASSERT(pairs[4] == 4);
	TEST_ASSERT(pairs[5] == 5);
	return true;
}

TEST(LeavesAsymmetricVerticesUnpaired) {
	std::vector<Vec3> body = SymmetricBody();
	body.push_back({7.0f, 50.0f, 0.0f}); // sem espelho: uma bolsa de um lado so

	const std::vector<int> pairs = PairMirroredVertices(body, 0.01f);
	TEST_ASSERT(pairs.back() == -1);

	// E os que tinham par continuam com ele.
	TEST_ASSERT(pairs[0] == 1);
	return true;
}

TEST(ToleranceDecidesWhatCountsAsAPair) {
	// Malha nenhuma e simetrica ao bit: o exportador arredonda e o par fica a
	// um centesimo de distancia.
	const std::vector<Vec3> almost = {
		{2.0f, 10.0f, 0.0f},
		{-2.004f, 10.0f, 0.0f},
	};

	// Tolerancia apertada nao casa.
	TEST_ASSERT(PairMirroredVertices(almost, 0.001f)[0] == -1);

	// Tolerancia folgada casa.
	TEST_ASSERT(PairMirroredVertices(almost, 0.01f)[0] == 1);

	// Tolerancia invalida nao pode inventar par nenhum.
	TEST_ASSERT(PairMirroredVertices(almost, 0.0f)[0] == -1);
	TEST_ASSERT(PairMirroredVertices(almost, -1.0f)[0] == -1);
	TEST_ASSERT(PairMirroredVertices({}, 0.01f).empty());

	// Com dois candidatos dentro da tolerancia, vence o mais perto -- nao o que
	// a grade devolveu primeiro.
	const std::vector<Vec3> crowded = {
		{2.0f, 0.0f, 0.0f},
		{-2.009f, 0.0f, 0.0f},
		{-2.001f, 0.0f, 0.0f},
	};
	TEST_ASSERT(PairMirroredVertices(crowded, 0.05f)[0] == 2);
	return true;
}

TEST(MirrorsTheMaskOntoTheOtherSide) {
	const std::vector<Vec3> body = SymmetricBody();
	const std::vector<int> pairs = PairMirroredVertices(body, 0.01f);

	// Mascarado so o lado direito, com borda suave.
	std::vector<float> mask = {1.0f, 0.0f, 0.4f, 0.0f, 0.0f, 0.0f};

	const std::vector<float> united = MirrorMask(mask, pairs, MirrorMode::Union);
	TEST_ASSERT(Near(united[0], 1.0f)); // continua
	TEST_ASSERT(Near(united[1], 1.0f)); // ganhou o espelho
	TEST_ASSERT(Near(united[2], 0.4f));
	TEST_ASSERT(Near(united[3], 0.4f)); // borda suave chega suave

	// A uniao e o MAIOR dos dois valores, nao um "ou" logico: a mascara e de 0
	// a 1, e tratar como booleano transformaria a borda num degrau.
	std::vector<float> partial = {0.3f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f};
	const std::vector<float> blended = MirrorMask(partial, pairs, MirrorMode::Union);
	TEST_ASSERT(Near(blended[0], 0.9f));
	TEST_ASSERT(Near(blended[1], 0.9f));

	// Replace passa o trabalho de um lado para o outro em vez de somar.
	const std::vector<float> replaced = MirrorMask(mask, pairs, MirrorMode::Replace);
	TEST_ASSERT(Near(replaced[0], 0.0f));
	TEST_ASSERT(Near(replaced[1], 1.0f));
	return true;
}

TEST(MaskMirrorProtectsWhatItCannotPair) {
	std::vector<Vec3> body = SymmetricBody();
	body.push_back({7.0f, 50.0f, 0.0f}); // sem espelho
	const std::vector<int> pairs = PairMirroredVertices(body, 0.01f);

	std::vector<float> mask = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

	// Vertice sem par mantem o que tinha, nos dois modos. Zerar o que nao tem
	// espelho apagaria a mascara justamente nas partes assimetricas da roupa,
	// que sao as que mais dao trabalho de mascarar.
	TEST_ASSERT(Near(MirrorMask(mask, pairs, MirrorMode::Union).back(), 1.0f));
	TEST_ASSERT(Near(MirrorMask(mask, pairs, MirrorMode::Replace).back(), 1.0f));

	// Mascara de tamanho diferente da malha volta intacta: aplicar assim mesmo
	// escreveria valores nos vertices errados.
	std::vector<float> wrongSize = {1.0f, 0.0f};
	const std::vector<float> untouched = MirrorMask(wrongSize, pairs, MirrorMode::Union);
	TEST_ASSERT(untouched.size() == 2);
	TEST_ASSERT(Near(untouched[0], 1.0f));
	return true;
}

TEST(PairingHandlesABodySizedMesh) {
	// A busca por par tem que ser local, nao uma varredura de todos contra
	// todos: um corpo do CBBE passa de cinquenta mil vertices, e o quadratico
	// seriam bilhoes de contas para espelhar uma mascara.
	std::vector<Vec3> body;
	const int columns = 120;
	const int rows = 120;
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < columns; ++c) {
			const float x = static_cast<float>(c) - static_cast<float>(columns) / 2.0f + 0.5f;
			body.push_back({x, static_cast<float>(r), 0.0f});
		}
	}
	TEST_ASSERT(body.size() == 14400);

	const std::vector<int> pairs = PairMirroredVertices(body, 0.01f);

	int paired = 0;
	for (size_t i = 0; i < pairs.size(); ++i) {
		if (pairs[i] < 0)
			continue;
		++paired;
		// Todo par tem que ser reciproco.
		TEST_ASSERT(pairs[static_cast<size_t>(pairs[i])] == static_cast<int>(i));
	}
	TEST_ASSERT(paired == static_cast<int>(body.size()));
	return true;
}
