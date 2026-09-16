#include "test_util.h"

#include <algorithm>

#include "mesh/seams.h"

namespace {

SeamMesh TwoPieces(bool splitUv) {
	SeamMesh mesh;
	mesh.vertices = {
		{0, 0, 0, 0, 0, true}, {1, 0, 0, 1, 0, true}, {0, 1, 0, 0, 1, true},
		{0, 0, 0, splitUv ? 0.5f : 0.0f, 0, true},
		{-1, 0, 0, 1, 0, true}, {0, -1, 0, 0, 1, true},
	};
	mesh.triangles = {{0, 1, 2}, {3, 4, 5}};
	return mesh;
}

} // namespace

TEST(FindsCoincidentGeometricSeamVertices) {
	const auto result = FindSeamVertices(TwoPieces(false), SeamKind::Geometry);
	TEST_ASSERT(result.size() == 2);
	TEST_ASSERT(result[0] == 0 && result[1] == 3);
	return true;
}

TEST(UvSeamsAreTheUvSplitSubsetOfGeometrySeams) {
	TEST_ASSERT(FindSeamVertices(TwoPieces(false), SeamKind::UV).empty());
	const auto result = FindSeamVertices(TwoPieces(true), SeamKind::UV);
	TEST_ASSERT(result.size() == 2);
	TEST_ASSERT(result[0] == 0 && result[1] == 3);
	return true;
}

TEST(NonManifoldIncludesOneVertexWideOpenBorders) {
	SeamMesh mesh;
	mesh.vertices.resize(3);
	mesh.triangles = {{0, 1, 2}};
	const auto result = FindSeamVertices(mesh, SeamKind::NonManifold);
	TEST_ASSERT(result.size() == 3);
	return true;
}

TEST(HumanoidOpeningsIgnoreTheMiddleBoundary) {
	SeamMesh mesh;
	mesh.vertices = {
		{-5, 0, 50}, {-5, 1, 50}, {-5, 0, 51}, // left wrist
		{5, 0, 50}, {5, 1, 50}, {5, 0, 51},    // right wrist
		{-1, 0, 0}, {1, 0, 0}, {0, 1, 0},      // ankle
		{-1, 0, 100}, {1, 0, 100}, {0, 1, 100}, // neck
		{-1, 0, 50}, {1, 0, 50}, {0, 1, 50},    // central seam
	};
	mesh.triangles = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}, {12, 13, 14}};
	const auto result = FindSeamVertices(mesh, SeamKind::HumanoidOpenings);
	TEST_ASSERT(result.size() == 12);
	for (uint16_t index : result)
		TEST_ASSERT(index < 12);
	return true;
}

TEST(HumanoidOpeningsHandlePermutedInternalAxes) {
	SeamMesh mesh;
	mesh.vertices = {
		{0, -5, 50}, {1, -5, 50}, {0, -5, 51}, // left wrist
		{0, 5, 50}, {1, 5, 50}, {0, 5, 51},    // right wrist
		{0, -1, 0}, {0, 1, 0}, {1, 0, 0},      // ankle
		{0, -1, 100}, {0, 1, 100}, {1, 0, 100}, // neck
		{0, -1, 50}, {0, 1, 50}, {1, 0, 50},    // central seam
	};
	mesh.triangles = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}, {12, 13, 14}};
	const auto result = FindSeamVertices(mesh, SeamKind::HumanoidOpenings);
	TEST_ASSERT(result.size() == 12);
	for (uint16_t index : result)
		TEST_ASSERT(index < 12);
	return true;
}

TEST(HumanoidOpeningsStopWhenTheBoundaryTurnsUpTheLimb) {
	SeamMesh mesh;
	mesh.vertices = {
		{-1, 0, 0}, {0, -1, 0}, {1, 0, 0}, // ankle rim
		{1, 0, 15}, {-1, 0, 15},            // connected seam tails
		{-5, 0, 50}, {5, 0, 50}, {0, 1, 100}, {0, -1, 50}, // closed body bounds
		{0, 0, 5}, // fan center
	};
	mesh.triangles = {
		{0, 1, 9}, {1, 2, 9}, {2, 3, 9}, {3, 4, 9}, {4, 0, 9},
		{5, 6, 7}, {5, 8, 6}, {6, 8, 7}, {7, 8, 5},
	};
	const auto result = FindSeamVertices(mesh, SeamKind::HumanoidOpenings);
	TEST_ASSERT(std::find(result.begin(), result.end(), 0) != result.end());
	TEST_ASSERT(std::find(result.begin(), result.end(), 1) != result.end());
	TEST_ASSERT(std::find(result.begin(), result.end(), 2) != result.end());
	TEST_ASSERT(std::find(result.begin(), result.end(), 3) == result.end());
	TEST_ASSERT(std::find(result.begin(), result.end(), 4) == result.end());
	return true;
}
