#include "mesh/mirror.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace {

// Celula de uma grade espacial. A busca por par tem que ser uma consulta local,
// nao uma varredura: um corpo do CBBE passa de cinquenta mil vertices, e
// comparar todos contra todos seriam bilhoes de contas para espelhar uma
// mascara.
struct Cell {
	int x = 0;
	int y = 0;
	int z = 0;

	bool operator==(const Cell& other) const {
		return x == other.x && y == other.y && z == other.z;
	}
};

struct CellHash {
	size_t operator()(const Cell& cell) const {
		// Tres primos grandes, o suficiente para nao amontoar coordenadas
		// alinhadas -- e malha de roupa e cheia de vertice alinhado.
		const uint64_t h = static_cast<uint64_t>(cell.x) * 73856093u ^
						   static_cast<uint64_t>(cell.y) * 19349663u ^
						   static_cast<uint64_t>(cell.z) * 83492791u;
		return static_cast<size_t>(h);
	}
};

Cell CellOf(const Vec3& position, float size) {
	Cell cell;
	cell.x = static_cast<int>(std::floor(position.x / size));
	cell.y = static_cast<int>(std::floor(position.y / size));
	cell.z = static_cast<int>(std::floor(position.z / size));
	return cell;
}

float DistanceSquared(const Vec3& a, const Vec3& b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	const float dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}

} // namespace

std::vector<int> PairMirroredVertices(const std::vector<Vec3>& positions, float tolerance) {
	std::vector<int> pairs(positions.size(), -1);
	if (positions.empty() || !(tolerance > 0.0f))
		return pairs;

	// Celula do tamanho da tolerancia: assim um par so pode estar na propria
	// celula ou nas vinte e seis vizinhas, e nunca mais longe.
	const float cellSize = tolerance;

	std::unordered_map<Cell, std::vector<int>, CellHash> grid;
	grid.reserve(positions.size());
	for (int i = 0; i < static_cast<int>(positions.size()); ++i)
		grid[CellOf(positions[static_cast<size_t>(i)], cellSize)].push_back(i);

	const float toleranceSquared = tolerance * tolerance;

	for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
		const Vec3& self = positions[static_cast<size_t>(i)];

		Vec3 wanted;
		wanted.x = -self.x;
		wanted.y = self.y;
		wanted.z = self.z;

		const Cell center = CellOf(wanted, cellSize);

		int best = -1;
		float bestDistance = toleranceSquared;

		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dz = -1; dz <= 1; ++dz) {
					Cell neighbour;
					neighbour.x = center.x + dx;
					neighbour.y = center.y + dy;
					neighbour.z = center.z + dz;

					auto found = grid.find(neighbour);
					if (found == grid.end())
						continue;

					for (int candidate : found->second) {
						const float distance =
							DistanceSquared(wanted, positions[static_cast<size_t>(candidate)]);

						// Estritamente menor: com empate fica o primeiro, e o
						// pareamento nao pode depender da ordem em que a grade
						// devolveu os candidatos.
						if (distance < bestDistance) {
							bestDistance = distance;
							best = candidate;
						}
					}
				}
			}
		}

		pairs[static_cast<size_t>(i)] = best;
	}

	return pairs;
}

std::vector<float> MirrorMask(const std::vector<float>& mask, const std::vector<int>& pairs,
							  MirrorMode mode) {
	// Tamanhos diferentes significam que a mascara e a malha nao sao do mesmo
	// shape. Devolver a mascara intacta e a unica saida segura: aplicar assim
	// mesmo escreveria valores nos vertices errados.
	if (mask.size() != pairs.size())
		return mask;

	std::vector<float> out(mask.size(), 0.0f);

	for (size_t i = 0; i < mask.size(); ++i) {
		const int partner = pairs[i];

		// Sem par, o valor fica como estava nos dois modos: apagar o que nao
		// tem espelho destruiria a mascara nas partes assimetricas da roupa.
		if (partner < 0 || partner >= static_cast<int>(mask.size())) {
			out[i] = mask[i];
			continue;
		}

		const float mirrored = mask[static_cast<size_t>(partner)];
		out[i] = (mode == MirrorMode::Union) ? (mirrored > mask[i] ? mirrored : mask[i]) : mirrored;
	}

	return out;
}
