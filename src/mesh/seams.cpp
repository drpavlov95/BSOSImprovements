#include "mesh/seams.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace {

using Edge = std::pair<uint16_t, uint16_t>;
using Cell = std::tuple<int64_t, int64_t, int64_t>;

Edge SortedEdge(uint16_t a, uint16_t b) {
	return a < b ? Edge(a, b) : Edge(b, a);
}

float PositionDistanceSquared(const SeamVertex& a, const SeamVertex& b) {
	const float x = a.x - b.x;
	const float y = a.y - b.y;
	const float z = a.z - b.z;
	return x * x + y * y + z * z;
}

bool UvDiffers(const SeamVertex& a, const SeamVertex& b, float tolerance) {
	if (!a.hasUv || !b.hasUv)
		return false;
	const float u = a.u - b.u;
	const float v = a.v - b.v;
	return u * u + v * v > tolerance * tolerance;
}

int64_t Quantize(float value, float cellSize) {
	return static_cast<int64_t>(std::floor(static_cast<double>(value) / cellSize));
}

} // namespace

std::vector<uint16_t> FindSeamVertices(const SeamMesh& mesh, SeamKind kind,
									   float positionTolerance, float uvTolerance) {
	if (mesh.vertices.empty() || positionTolerance <= 0.0f)
		return {};

	std::map<Edge, unsigned int> edgeUses;
	for (const SeamTriangle& tri : mesh.triangles) {
		if (tri.a >= mesh.vertices.size() || tri.b >= mesh.vertices.size() ||
			tri.c >= mesh.vertices.size())
			continue;
		++edgeUses[SortedEdge(tri.a, tri.b)];
		++edgeUses[SortedEdge(tri.b, tri.c)];
		++edgeUses[SortedEdge(tri.c, tri.a)];
	}

	std::set<uint16_t> selected;
	std::vector<uint16_t> boundary;
	std::vector<bool> isBoundary(mesh.vertices.size(), false);
	std::vector<std::vector<uint16_t>> boundaryNeighbors(mesh.vertices.size());
	std::vector<std::vector<uint16_t>> meshNeighbors(mesh.vertices.size());
	for (const auto& [edge, uses] : edgeUses) {
		meshNeighbors[edge.first].push_back(edge.second);
		meshNeighbors[edge.second].push_back(edge.first);
		if (uses == 1) {
			isBoundary[edge.first] = true;
			isBoundary[edge.second] = true;
			boundaryNeighbors[edge.first].push_back(edge.second);
			boundaryNeighbors[edge.second].push_back(edge.first);
		}
		if (kind == SeamKind::NonManifold && uses != 2) {
			selected.insert(edge.first);
			selected.insert(edge.second);
		}
	}
	if (kind == SeamKind::NonManifold)
		return std::vector<uint16_t>(selected.begin(), selected.end());

	for (size_t i = 0; i < isBoundary.size(); ++i)
		if (isBoundary[i])
			boundary.push_back(static_cast<uint16_t>(i));

	// Alguns corpos antigos unem a linha central/coronal as aberturas externas
	// na mesma componente de borda. Para esse caso, separamos as aberturas pela
	// sua posicao anatomica: extremos laterais (pulsos), extremo inferior
	// (tornozelos) e extremo superior (pescoco). Isso mantem a faixa com um
	// vertice e deixa a costura central de fora.
	if (kind == SeamKind::HumanoidOpenings) {
		float minimum[3] = {mesh.vertices.front().x, mesh.vertices.front().y,
							 mesh.vertices.front().z};
		float maximum[3] = {minimum[0], minimum[1], minimum[2]};
		for (const SeamVertex& vertex : mesh.vertices) {
			const float coordinate[3] = {vertex.x, vertex.y, vertex.z};
			for (int axis = 0; axis < 3; ++axis) {
				minimum[axis] = std::min(minimum[axis], coordinate[axis]);
				maximum[axis] = std::max(maximum[axis], coordinate[axis]);
			}
		}
		float span[3] = {maximum[0] - minimum[0], maximum[1] - minimum[1],
						 maximum[2] - minimum[2]};
		int verticalAxis = 0;
		if (span[1] > span[verticalAxis]) verticalAxis = 1;
		if (span[2] > span[verticalAxis]) verticalAxis = 2;
		int lateralAxis = verticalAxis == 0 ? 1 : 0;
		for (int axis = 0; axis < 3; ++axis)
			if (axis != verticalAxis && span[axis] > span[lateralAxis])
				lateralAxis = axis;
		if (span[lateralAxis] <= 0.0f || span[verticalAxis] <= 0.0f)
			return {};

		auto coordinate = [](const SeamVertex& vertex, int axis) {
			if (axis == 0) return vertex.x;
			if (axis == 1) return vertex.y;
			return vertex.z;
		};
		auto traceRim = [&](uint16_t seed, int fallbackAxis, float fallbackSign) {
			float direction[3] = {0.0f, 0.0f, 0.0f};
			const SeamVertex& seedVertex = mesh.vertices[seed];
			for (uint16_t neighbor : meshNeighbors[seed]) {
				if (isBoundary[neighbor])
					continue;
				const SeamVertex& inside = mesh.vertices[neighbor];
				direction[0] += seedVertex.x - inside.x;
				direction[1] += seedVertex.y - inside.y;
				direction[2] += seedVertex.z - inside.z;
			}
			float directionLength = std::sqrt(direction[0] * direction[0] +
				direction[1] * direction[1] + direction[2] * direction[2]);
			if (directionLength <= 0.0f) {
				direction[fallbackAxis] = fallbackSign;
				directionLength = 1.0f;
			}
			for (float& value : direction)
				value /= directionLength;

			std::vector<uint16_t> pending = {seed};
			std::vector<bool> visited(mesh.vertices.size(), false);
			while (!pending.empty()) {
				const uint16_t current = pending.back();
				pending.pop_back();
				if (visited[current])
					continue;
				visited[current] = true;
				selected.insert(current);
				for (uint16_t next : boundaryNeighbors[current]) {
					const SeamVertex& a = mesh.vertices[current];
					const SeamVertex& b = mesh.vertices[next];
					const float dx = b.x - a.x;
					const float dy = b.y - a.y;
					const float dz = b.z - a.z;
					const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (length <= 0.0f)
						continue;
					const float alongLimb = std::abs(dx * direction[0] + dy * direction[1] +
						dz * direction[2]);
					// A direcao vem da fileira interior logo atras da abertura. Assim
					// cortes inclinados continuam sendo percorridos por inteiro, mas a
					// cauda que segue para dentro do membro encerra o percurso.
					if (alongLimb / length <= 0.70f && !visited[next])
						pending.push_back(next);
				}
			}
		};

		std::vector<bool> componentVisited(mesh.vertices.size(), false);
		for (uint16_t start : boundary) {
			if (componentVisited[start])
				continue;
			std::vector<uint16_t> component;
			std::vector<uint16_t> pending = {start};
			componentVisited[start] = true;
			while (!pending.empty()) {
				const uint16_t current = pending.back();
				pending.pop_back();
				component.push_back(current);
				for (uint16_t next : boundaryNeighbors[current])
					if (!componentVisited[next]) {
						componentVisited[next] = true;
						pending.push_back(next);
					}
			}

			uint16_t low = component.front();
			uint16_t high = component.front();
			uint16_t left = component.front();
			uint16_t right = component.front();
			for (uint16_t index : component) {
				if (coordinate(mesh.vertices[index], verticalAxis) < coordinate(mesh.vertices[low], verticalAxis)) low = index;
				if (coordinate(mesh.vertices[index], verticalAxis) > coordinate(mesh.vertices[high], verticalAxis)) high = index;
				if (coordinate(mesh.vertices[index], lateralAxis) < coordinate(mesh.vertices[left], lateralAxis)) left = index;
				if (coordinate(mesh.vertices[index], lateralAxis) > coordinate(mesh.vertices[right], lateralAxis)) right = index;
			}
			const float componentLow = coordinate(mesh.vertices[low], verticalAxis);
			const float componentHigh = coordinate(mesh.vertices[high], verticalAxis);
			const float componentLeft = coordinate(mesh.vertices[left], lateralAxis);
			const float componentRight = coordinate(mesh.vertices[right], lateralAxis);

			const bool reachesNeck = componentHigh >= maximum[verticalAxis] - span[verticalAxis] * 0.08f;
			const bool reachesAnkle = componentLow <= minimum[verticalAxis] + span[verticalAxis] * 0.08f;
			const bool reachesLeftWrist = componentLeft <= minimum[lateralAxis] + span[lateralAxis] * 0.08f;
			const bool reachesRightWrist = componentRight >= maximum[lateralAxis] - span[lateralAxis] * 0.08f;
			if (reachesNeck) {
				const float componentHeight = componentHigh - componentLow;
				if (componentHeight <= span[verticalAxis] * 0.08f)
					selected.insert(component.begin(), component.end());
				else
					traceRim(high, verticalAxis, 1.0f);
			}
			if (reachesAnkle)
				traceRim(low, verticalAxis, -1.0f);
			if (reachesLeftWrist)
				traceRim(left, lateralAxis, -1.0f);
			if (reachesRightWrist)
				traceRim(right, lateralAxis, 1.0f);
		}
		return std::vector<uint16_t>(selected.begin(), selected.end());
	}

	// Grade espacial: evita comparar cada borda com todas as demais. Os 27
	// vizinhos ainda sao visitados para nao perder um par que caiu em lados
	// diferentes de uma fronteira da grade.
	std::map<Cell, std::vector<uint16_t>> cells;
	const float tolerance2 = positionTolerance * positionTolerance;
	for (uint16_t index : boundary) {
		const SeamVertex& vertex = mesh.vertices[index];
		const int64_t x = Quantize(vertex.x, positionTolerance);
		const int64_t y = Quantize(vertex.y, positionTolerance);
		const int64_t z = Quantize(vertex.z, positionTolerance);

		for (int dx = -1; dx <= 1; ++dx)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dz = -1; dz <= 1; ++dz) {
					auto found = cells.find(Cell(x + dx, y + dy, z + dz));
					if (found == cells.end())
						continue;
					for (uint16_t other : found->second) {
						const SeamVertex& candidate = mesh.vertices[other];
						if (PositionDistanceSquared(vertex, candidate) > tolerance2)
							continue;
						if (kind == SeamKind::Geometry || UvDiffers(vertex, candidate, uvTolerance)) {
							selected.insert(index);
							selected.insert(other);
						}
					}
				}
		cells[Cell(x, y, z)].push_back(index);
	}

	return std::vector<uint16_t>(selected.begin(), selected.end());
}
