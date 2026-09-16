#pragma once

#include <cstdint>
#include <vector>

enum class SeamKind { Geometry, UV, NonManifold, HumanoidOpenings };

struct SeamVertex {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
	bool hasUv = false;
};

struct SeamTriangle {
	uint16_t a = 0;
	uint16_t b = 0;
	uint16_t c = 0;
};

struct SeamMesh {
	std::vector<SeamVertex> vertices;
	std::vector<SeamTriangle> triangles;
};

// Devolve uma faixa de exatamente um vertice. Geometric seams sao bordas
// topologicamente separadas que ocupam a mesma posicao; UV seams sao o
// subconjunto cujas coordenadas UV divergem; non-manifold inclui bordas abertas
// e arestas usadas por mais de dois triangulos. HumanoidOpenings identifica as
// aberturas externas de pescoco, pulsos e tornozelos de um corpo ereto.
std::vector<uint16_t> FindSeamVertices(const SeamMesh& mesh, SeamKind kind,
									   float positionTolerance = 0.0001f,
									   float uvTolerance = 0.0001f);
