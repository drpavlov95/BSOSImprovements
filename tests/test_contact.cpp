#include "test_util.h"

#include <vector>

#include "mesh/contact.h"

namespace {

// Um triangulo solto, na posicao que o teste pedir. Serve de mesh minima: o
// grafo de contato so precisa de posicoes e triangulos.
struct Piece {
	std::vector<Vec3> positions;
	std::vector<int> triangles;

	ContactMesh Ref() const {
		ContactMesh mesh;
		mesh.positions = positions.data();
		mesh.vertexCount = positions.size();
		mesh.triangles = triangles.data();
		mesh.triangleCount = triangles.size() / 3;
		return mesh;
	}
};

} // namespace

TEST(CoincidentVerticesOfTwoMeshesBecomeOneCluster) {
	// Duas meshes que se tocam num vertice so: o 0 de cada uma.
	Piece body;
	body.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	body.triangles = {0, 1, 2};

	Piece head;
	head.positions = {{0, 0, 0}, {-1, 0, 0}, {0, -1, 0}};
	head.triangles = {0, 1, 2};

	const ContactGraph graph = BuildContactGraph({body.Ref(), head.Ref()}, 0.001f);

	TEST_ASSERT(graph.clusters.size() == 1);
	TEST_ASSERT(graph.clusters[0].members.size() == 2);
	TEST_ASSERT(graph.clusters[0].members[0].mesh == 0);
	TEST_ASSERT(graph.clusters[0].members[0].vertex == 0);
	TEST_ASSERT(graph.clusters[0].members[1].mesh == 1);
	TEST_ASSERT(graph.clusters[0].members[1].vertex == 0);
	return true;
}

TEST(ThreeMeshesMeetingAtOnePointShareASingleCluster) {
	Piece a;
	a.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	a.triangles = {0, 1, 2};

	Piece b;
	b.positions = {{0, 0, 0}, {-1, 0, 0}, {0, -1, 0}};
	b.triangles = {0, 1, 2};

	Piece c;
	c.positions = {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}};
	c.triangles = {0, 1, 2};

	const ContactGraph graph = BuildContactGraph({a.Ref(), b.Ref(), c.Ref()}, 0.001f);

	TEST_ASSERT(graph.clusters.size() == 1);
	TEST_ASSERT(graph.clusters[0].members.size() == 3);
	return true;
}

TEST(VerticesFartherApartThanTheToleranceAreNotContact) {
	Piece a;
	a.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	a.triangles = {0, 1, 2};

	Piece b;
	b.positions = {{0.02f, 0, 0}, {-1, 0, 0}, {0, -1, 0}};
	b.triangles = {0, 1, 2};

	TEST_ASSERT(BuildContactGraph({a.Ref(), b.Ref()}, 0.01f).clusters.empty());
	TEST_ASSERT(BuildContactGraph({a.Ref(), b.Ref()}, 0.05f).clusters.size() == 1);
	return true;
}

TEST(DuplicatedVerticesInsideOneMeshAreNotContactByThemselves) {
	// O anel de UV seam de uma mesh so duplica vertices na mesma posicao. Isso e
	// assunto do host, e nao contato entre pecas.
	Piece lonely;
	lonely.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 0}, {-1, 0, 0}, {0, -1, 0}};
	lonely.triangles = {0, 1, 2, 3, 4, 5};

	TEST_ASSERT(BuildContactGraph({lonely.Ref()}, 0.001f).clusters.empty());
	return true;
}

TEST(AVertexLandingOnAnotherMeshsEdgeIsRecordedAsARatio) {
	// A denso: a aresta 0-1 vai de (0,0,0) a (2,0,0).
	Piece dense;
	dense.positions = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
	dense.triangles = {0, 1, 2};

	// B esparso: o vertice 0 cai no meio daquela aresta, sem par para chamar de
	// seu. Os outros dois ficam longe de tudo.
	Piece sparse;
	sparse.positions = {{1, 0, 0}, {1, -2, 0}, {2, -2, 0}};
	sparse.triangles = {0, 1, 2};

	const ContactGraph graph = BuildContactGraph({dense.Ref(), sparse.Ref()}, 0.001f);

	TEST_ASSERT(graph.clusters.empty());
	TEST_ASSERT(graph.onEdges.size() == 1);
	const ContactOnEdge& contact = graph.onEdges[0];
	TEST_ASSERT(contact.follower.mesh == 1 && contact.follower.vertex == 0);
	TEST_ASSERT(contact.edgeStart.mesh == 0 && contact.edgeStart.vertex == 0);
	TEST_ASSERT(contact.edgeEnd.mesh == 0 && contact.edgeEnd.vertex == 1);
	TEST_ASSERT(contact.ratio > 0.49f && contact.ratio < 0.51f);
	return true;
}

namespace {

Piece Body() {
	Piece piece;
	piece.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	piece.triangles = {0, 1, 2};
	return piece;
}

Piece Head() {
	Piece piece;
	piece.positions = {{0, 0, 0}, {-1, 0, 0}, {0, -1, 0}};
	piece.triangles = {0, 1, 2};
	return piece;
}

bool Near(const Vec3& value, float x, float y, float z) {
	const float tolerance = 0.0001f;
	return value.x > x - tolerance && value.x < x + tolerance && value.y > y - tolerance &&
		   value.y < y + tolerance && value.z > z - tolerance && value.z < z + tolerance;
}

} // namespace

TEST(TheClusterMemberThatDidNotMoveFollowsTheOneThatDid) {
	Piece body = Body();
	Piece head = Head();
	const ContactGraph graph = BuildContactGraph({body.Ref(), head.Ref()}, 0.001f);

	// O pincel inflou o corpo. A cabeca nao foi tocada.
	body.positions[0] = {0, 5, 0};

	const std::vector<ContactCorrection> corrections =
		SolveContacts(graph, {body.Ref(), head.Ref()}, {{0, 0}});

	TEST_ASSERT(corrections.size() == 1);
	TEST_ASSERT(corrections[0].vertex.mesh == 1 && corrections[0].vertex.vertex == 0);
	TEST_ASSERT(Near(corrections[0].position, 0, 5, 0));
	return true;
}

TEST(WhenBothSidesMoveTheClusterMeetsAtTheAverage) {
	Piece body = Body();
	Piece head = Head();
	const ContactGraph graph = BuildContactGraph({body.Ref(), head.Ref()}, 0.001f);

	body.positions[0] = {0, 4, 0};
	head.positions[0] = {0, 6, 0};

	const std::vector<ContactCorrection> corrections =
		SolveContacts(graph, {body.Ref(), head.Ref()}, {{0, 0}, {1, 0}});

	TEST_ASSERT(corrections.size() == 2);
	for (const ContactCorrection& correction : corrections)
		TEST_ASSERT(Near(correction.position, 0, 5, 0));
	return true;
}

TEST(AVertexOnAnEdgeKeepsItsRatioWhenTheEdgeMoves) {
	Piece dense;
	dense.positions = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
	dense.triangles = {0, 1, 2};

	Piece sparse;
	sparse.positions = {{1, 0, 0}, {1, -2, 0}, {2, -2, 0}};
	sparse.triangles = {0, 1, 2};

	const ContactGraph graph = BuildContactGraph({dense.Ref(), sparse.Ref()}, 0.001f);

	// A aresta inteira subiu em Z. O vertice esparso nao foi tocado.
	dense.positions[0] = {0, 0, 2};
	dense.positions[1] = {2, 0, 2};

	const std::vector<ContactCorrection> corrections =
		SolveContacts(graph, {dense.Ref(), sparse.Ref()}, {{0, 0}, {0, 1}});

	TEST_ASSERT(corrections.size() == 1);
	TEST_ASSERT(corrections[0].vertex.mesh == 1 && corrections[0].vertex.vertex == 0);
	TEST_ASSERT(Near(corrections[0].position, 1, 0, 2));
	return true;
}

TEST(NothingMovedMeansNothingToCorrect) {
	Piece body = Body();
	Piece head = Head();
	const ContactGraph graph = BuildContactGraph({body.Ref(), head.Ref()}, 0.001f);

	TEST_ASSERT(SolveContacts(graph, {body.Ref(), head.Ref()}, {}).empty());
	return true;
}

TEST(TheCorrectionFadesIntoTheInteriorOverTheGivenRings) {
	// A: um triangulo cujo vertice 0 encosta na tira de B.
	Piece pull;
	pull.positions = {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	pull.triangles = {0, 1, 2};

	// B: uma tira de dois quads. A partir do vertice 0, o anel 1 e {1, 3} e o
	// anel 2 e {2, 4}.
	Piece strip;
	strip.positions = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, -1, 0}, {1, -1, 0}, {2, -1, 0}};
	strip.triangles = {0, 1, 3, 1, 4, 3, 1, 2, 4, 2, 5, 4};

	const ContactGraph graph = BuildContactGraph({pull.Ref(), strip.Ref()}, 0.001f);
	TEST_ASSERT(graph.clusters.size() == 1);
	TEST_ASSERT(graph.onEdges.empty());

	// O pincel levantou o vertice de A em Z.
	pull.positions[0] = {0, 0, 1};

	const std::vector<ContactCorrection> corrections =
		SolveContacts(graph, {pull.Ref(), strip.Ref()}, {{0, 0}}, 2);

	// O anel corrigido vai inteiro; o anel 1 vai pela metade; o anel 2 fica onde
	// esta, senao nao teria decaido a zero.
	TEST_ASSERT(corrections.size() == 3);
	for (const ContactCorrection& correction : corrections) {
		TEST_ASSERT(correction.vertex.mesh == 1);
		const int vertex = correction.vertex.vertex;
		if (vertex == 0)
			TEST_ASSERT(Near(correction.position, 0, 0, 1));
		else if (vertex == 1)
			TEST_ASSERT(Near(correction.position, 1, 0, 0.5f));
		else if (vertex == 3)
			TEST_ASSERT(Near(correction.position, 0, -1, 0.5f));
		else
			TEST_ASSERT(false);
	}
	return true;
}

TEST(WithoutRingsOnlyTheContactItselfMoves) {
	Piece pull;
	pull.positions = {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	pull.triangles = {0, 1, 2};

	Piece strip;
	strip.positions = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, -1, 0}, {1, -1, 0}, {2, -1, 0}};
	strip.triangles = {0, 1, 3, 1, 4, 3, 1, 2, 4, 2, 5, 4};

	const ContactGraph graph = BuildContactGraph({pull.Ref(), strip.Ref()}, 0.001f);
	pull.positions[0] = {0, 0, 1};

	const std::vector<ContactCorrection> corrections =
		SolveContacts(graph, {pull.Ref(), strip.Ref()}, {{0, 0}}, 1);

	TEST_ASSERT(corrections.size() == 1);
	TEST_ASSERT(corrections[0].vertex.vertex == 0);
	return true;
}

TEST(ContactAcrossACellBoundaryStillPairs) {
	// Os dois vertices estao a 0.0002 um do outro, mas caem em lados opostos de
	// uma fronteira de celula de 0.01. Um indice espacial que so olhasse a
	// propria celula perderia este par.
	Piece a;
	a.positions = {{0.0099f, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	a.triangles = {0, 1, 2};

	Piece b;
	b.positions = {{0.0101f, 0, 0}, {-1, 0, 0}, {0, -1, 0}};
	b.triangles = {0, 1, 2};

	TEST_ASSERT(BuildContactGraph({a.Ref(), b.Ref()}, 0.01f).clusters.size() == 1);
	return true;
}

namespace {

// Uma grade triangulada de cols x rows, com espacamento de 0.1 a partir de
// baseY. Serve de mesh do tamanho de um corpo de verdade.
Piece Grid(int cols, int rows, float baseY) {
	Piece piece;
	for (int row = 0; row < rows; ++row)
		for (int col = 0; col < cols; ++col)
			piece.positions.push_back(
				{static_cast<float>(col) * 0.1f, baseY + static_cast<float>(row) * 0.1f, 0.0f});
	for (int row = 0; row + 1 < rows; ++row) {
		for (int col = 0; col + 1 < cols; ++col) {
			const int corner = row * cols + col;
			piece.triangles.insert(piece.triangles.end(),
								   {corner, corner + 1, corner + cols});
			piece.triangles.insert(piece.triangles.end(),
								   {corner + 1, corner + cols + 1, corner + cols});
		}
	}
	return piece;
}

} // namespace

TEST(BuildingTheGraphOfTwoBodySizedMeshesStaysInteractive) {
	// Duas grades de 8000 vertices que se encostam numa linha de 100.
	const Piece above = Grid(100, 80, 0.0f);
	const Piece below = Grid(100, 80, -7.9f);
	TEST_ASSERT(above.positions.size() == 8000);

	const ULONGLONG started = GetTickCount64();
	const ContactGraph graph = BuildContactGraph({above.Ref(), below.Ref()}, 0.01f);
	const ULONGLONG elapsed = GetTickCount64() - started;

	TEST_ASSERT(graph.clusters.size() == 100);
	TEST_ASSERT(graph.onEdges.empty());

	// O usuario aperta o botao e espera a resposta. A margem e enorme de
	// proposito: o que este numero derruba e comportamento quadratico, nao
	// diferenca de maquina.
	std::printf("[%llu ms] ", elapsed);
	TEST_ASSERT(elapsed < 1000);
	return true;
}
