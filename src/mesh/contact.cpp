#include "mesh/contact.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace {

struct FlatVertex {
	int mesh = 0;
	int vertex = 0;
	Vec3 position;
};

int Root(std::vector<int>& parent, int index) {
	while (parent[index] != index) {
		parent[index] = parent[parent[index]];
		index = parent[index];
	}
	return index;
}

void Join(std::vector<int>& parent, int a, int b) {
	const int rootA = Root(parent, a);
	const int rootB = Root(parent, b);
	if (rootA != rootB)
		parent[rootB] = rootA;
}

float DistanceSquared(const Vec3& a, const Vec3& b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	const float dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}

// Indice espacial. A celula e do tamanho da tolerancia no passe 1, e do tamanho
// da aresta media no passe 2 -- uma celula pequena demais faria cada aresta
// ocupar milhares delas.
//
// Colisao de hash nao erra resultado: a celula errada so entra como candidata e
// morre no teste de distancia. Custa tempo, nao correcao.
using SpatialGrid = std::unordered_map<uint64_t, std::vector<int>>;

int CellOf(float value, float cell) {
	return static_cast<int>(std::floor(value / cell));
}

uint64_t CellHash(int x, int y, int z) {
	uint64_t hash = static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x9E3779B97F4A7C15ull;
	hash ^= static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0xC2B2AE3D27D4EB4Full;
	hash ^= static_cast<uint64_t>(static_cast<uint32_t>(z)) * 0x165667B19E3779F9ull;
	return hash;
}

struct Edge {
	int mesh = 0;
	int start = 0; // sempre o menor indice, para a razao nao depender da ordem
	int end = 0;   // em que o triangulo listou os vertices
};

// Arestas de todos os triangulos, sem repetir as que dois triangulos dividem.
std::vector<Edge> CollectEdges(const std::vector<ContactMesh>& meshes) {
	std::vector<Edge> edges;
	for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		const ContactMesh& mesh = meshes[meshIndex];
		if (!mesh.triangles || !mesh.positions)
			continue;
		for (size_t triangle = 0; triangle < mesh.triangleCount; ++triangle) {
			const int* corner = mesh.triangles + triangle * 3;
			for (int side = 0; side < 3; ++side) {
				const int a = corner[side];
				const int b = corner[(side + 1) % 3];
				if (a == b || a < 0 || b < 0 || static_cast<size_t>(a) >= mesh.vertexCount ||
					static_cast<size_t>(b) >= mesh.vertexCount)
					continue;
				edges.push_back({static_cast<int>(meshIndex), (std::min)(a, b), (std::max)(a, b)});
			}
		}
	}
	std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
		if (a.mesh != b.mesh)
			return a.mesh < b.mesh;
		if (a.start != b.start)
			return a.start < b.start;
		return a.end < b.end;
	});
	edges.erase(std::unique(edges.begin(), edges.end(),
							[](const Edge& a, const Edge& b) {
								return a.mesh == b.mesh && a.start == b.start && a.end == b.end;
							}),
				edges.end());
	return edges;
}

} // namespace

ContactGraph BuildContactGraph(const std::vector<ContactMesh>& meshes, float tolerance) {
	std::vector<FlatVertex> flat;
	for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		const ContactMesh& mesh = meshes[meshIndex];
		if (!mesh.positions)
			continue;
		for (size_t vertex = 0; vertex < mesh.vertexCount; ++vertex)
			flat.push_back({static_cast<int>(meshIndex), static_cast<int>(vertex),
							mesh.positions[vertex]});
	}

	// Une so o que vem de meshes diferentes: vertices duplicados dentro da mesma
	// mesh sao assunto do host, nao contato entre pecas. A transitividade cuida
	// do resto -- se A1 e A2 tocam B1, os tres acabam no mesmo cluster.
	std::vector<int> parent(flat.size());
	std::iota(parent.begin(), parent.end(), 0);
	const float toleranceSquared = tolerance * tolerance;
	const float cell = tolerance > 0.0f ? tolerance : 1e-6f;

	SpatialGrid vertexGrid;
	for (size_t i = 0; i < flat.size(); ++i) {
		const Vec3& position = flat[i].position;
		vertexGrid[CellHash(CellOf(position.x, cell), CellOf(position.y, cell),
							CellOf(position.z, cell))]
			.push_back(static_cast<int>(i));
	}

	// As 27 celulas vizinhas, e nao so a propria: dois vertices a meio
	// micrometro um do outro caem em celulas diferentes quando a fronteira passa
	// entre eles.
	for (size_t i = 0; i < flat.size(); ++i) {
		const Vec3& position = flat[i].position;
		const int baseX = CellOf(position.x, cell);
		const int baseY = CellOf(position.y, cell);
		const int baseZ = CellOf(position.z, cell);
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dz = -1; dz <= 1; ++dz) {
					const auto bucket =
						vertexGrid.find(CellHash(baseX + dx, baseY + dy, baseZ + dz));
					if (bucket == vertexGrid.end())
						continue;
					for (int other : bucket->second) {
						const size_t j = static_cast<size_t>(other);
						if (j <= i || flat[i].mesh == flat[j].mesh)
							continue;
						if (DistanceSquared(position, flat[j].position) <= toleranceSquared)
							Join(parent, static_cast<int>(i), static_cast<int>(j));
					}
				}
			}
		}
	}

	std::vector<std::vector<int>> groups(flat.size());
	for (size_t i = 0; i < flat.size(); ++i)
		groups[static_cast<size_t>(Root(parent, static_cast<int>(i)))].push_back(
			static_cast<int>(i));

	ContactGraph graph;
	std::vector<bool> paired(flat.size(), false);
	for (const std::vector<int>& group : groups) {
		if (group.size() < 2)
			continue;
		for (int index : group)
			paired[static_cast<size_t>(index)] = true;
		ContactCluster cluster;
		for (int index : group)
			cluster.members.push_back({flat[static_cast<size_t>(index)].mesh,
									   flat[static_cast<size_t>(index)].vertex});
		std::sort(cluster.members.begin(), cluster.members.end(),
				  [](const ContactVertex& a, const ContactVertex& b) {
					  return a.mesh != b.mesh ? a.mesh < b.mesh : a.vertex < b.vertex;
				  });
		graph.clusters.push_back(std::move(cluster));
	}

	std::sort(graph.clusters.begin(), graph.clusters.end(),
			  [](const ContactCluster& a, const ContactCluster& b) {
				  const ContactVertex& first = a.members.front();
				  const ContactVertex& second = b.members.front();
				  return first.mesh != second.mesh ? first.mesh < second.mesh
												   : first.vertex < second.vertex;
			  });

	// Passe 2. So os vertices que sobraram do passe 1 entram: quem ja tem par
	// nao precisa de aresta para seguir.
	const std::vector<Edge> edges = CollectEdges(meshes);

	// A celula aqui e do tamanho da aresta media. Com a celula da tolerancia --
	// que e minuscula de proposito -- uma aresta sozinha ocuparia milhares.
	double lengthSum = 0.0;
	for (const Edge& edge : edges) {
		const ContactMesh& mesh = meshes[static_cast<size_t>(edge.mesh)];
		lengthSum += static_cast<double>(
			std::sqrt(DistanceSquared(mesh.positions[static_cast<size_t>(edge.start)],
									  mesh.positions[static_cast<size_t>(edge.end)])));
	}
	const float edgeCell =
		edges.empty() ? cell
					  : (std::max)(cell, static_cast<float>(lengthSum /
														   static_cast<double>(edges.size())));

	SpatialGrid edgeGrid;
	std::vector<int> sprawling; // as longas demais para caber no indice
	for (size_t e = 0; e < edges.size(); ++e) {
		const ContactMesh& mesh = meshes[static_cast<size_t>(edges[e].mesh)];
		const Vec3& a = mesh.positions[static_cast<size_t>(edges[e].start)];
		const Vec3& b = mesh.positions[static_cast<size_t>(edges[e].end)];
		const int lowX = CellOf((std::min)(a.x, b.x) - tolerance, edgeCell);
		const int lowY = CellOf((std::min)(a.y, b.y) - tolerance, edgeCell);
		const int lowZ = CellOf((std::min)(a.z, b.z) - tolerance, edgeCell);
		const int highX = CellOf((std::max)(a.x, b.x) + tolerance, edgeCell);
		const int highY = CellOf((std::max)(a.y, b.y) + tolerance, edgeCell);
		const int highZ = CellOf((std::max)(a.z, b.z) + tolerance, edgeCell);
		const int64_t spanned = static_cast<int64_t>(highX - lowX + 1) *
								static_cast<int64_t>(highY - lowY + 1) *
								static_cast<int64_t>(highZ - lowZ + 1);
		// Uma aresta muito mais longa que a media espalharia por celulas demais.
		// Sao poucas: sai do indice e passa a ser sempre candidata.
		if (spanned > 64) {
			sprawling.push_back(static_cast<int>(e));
			continue;
		}
		for (int x = lowX; x <= highX; ++x)
			for (int y = lowY; y <= highY; ++y)
				for (int z = lowZ; z <= highZ; ++z)
					edgeGrid[CellHash(x, y, z)].push_back(static_cast<int>(e));
	}

	for (size_t i = 0; i < flat.size(); ++i) {
		if (paired[i])
			continue;
		const FlatVertex& lonely = flat[i];
		float bestDistanceSquared = toleranceSquared;
		bool found = false;
		ContactOnEdge best;

		std::vector<int> candidates = sprawling;
		const int lowX = CellOf(lonely.position.x - tolerance, edgeCell);
		const int lowY = CellOf(lonely.position.y - tolerance, edgeCell);
		const int lowZ = CellOf(lonely.position.z - tolerance, edgeCell);
		const int highX = CellOf(lonely.position.x + tolerance, edgeCell);
		const int highY = CellOf(lonely.position.y + tolerance, edgeCell);
		const int highZ = CellOf(lonely.position.z + tolerance, edgeCell);
		for (int x = lowX; x <= highX; ++x) {
			for (int y = lowY; y <= highY; ++y) {
				for (int z = lowZ; z <= highZ; ++z) {
					const auto bucket = edgeGrid.find(CellHash(x, y, z));
					if (bucket != edgeGrid.end())
						candidates.insert(candidates.end(), bucket->second.begin(),
										  bucket->second.end());
				}
			}
		}

		for (int candidate : candidates) {
			const Edge& edge = edges[static_cast<size_t>(candidate)];
			if (edge.mesh == lonely.mesh)
				continue;
			const ContactMesh& mesh = meshes[static_cast<size_t>(edge.mesh)];
			const Vec3& start = mesh.positions[static_cast<size_t>(edge.start)];
			const Vec3& end = mesh.positions[static_cast<size_t>(edge.end)];
			const Vec3 along = {end.x - start.x, end.y - start.y, end.z - start.z};
			const float lengthSquared = along.x * along.x + along.y * along.y + along.z * along.z;
			if (lengthSquared <= 0.0f)
				continue;
			const float ratio = ((lonely.position.x - start.x) * along.x +
								 (lonely.position.y - start.y) * along.y +
								 (lonely.position.z - start.z) * along.z) /
								lengthSquared;
			// Intervalo aberto: as pontas ja sao caso do passe 1.
			if (ratio <= 0.0f || ratio >= 1.0f)
				continue;
			const Vec3 onEdge = {start.x + ratio * along.x, start.y + ratio * along.y,
								 start.z + ratio * along.z};
			const float distanceSquared = DistanceSquared(lonely.position, onEdge);
			if (distanceSquared > bestDistanceSquared)
				continue;
			bestDistanceSquared = distanceSquared;
			found = true;
			best.follower = {lonely.mesh, lonely.vertex};
			best.edgeStart = {edge.mesh, edge.start};
			best.edgeEnd = {edge.mesh, edge.end};
			best.ratio = ratio;
		}
		if (found)
			graph.onEdges.push_back(best);
	}
	return graph;
}

float AverageEdgeLength(const ContactMesh& mesh) {
	if (!mesh.positions || !mesh.triangles || mesh.vertexCount == 0 || mesh.triangleCount == 0)
		return 0.0f;

	const std::vector<Edge> edges = CollectEdges({mesh});
	if (edges.empty())
		return 0.0f;

	double total = 0.0;
	for (const Edge& edge : edges) {
		const Vec3& start = mesh.positions[static_cast<size_t>(edge.start)];
		const Vec3& end = mesh.positions[static_cast<size_t>(edge.end)];
		total += std::sqrt(static_cast<double>(DistanceSquared(start, end)));
	}
	return static_cast<float>(total / static_cast<double>(edges.size()));
}

int RingsForRadius(float radius, float averageEdgeLength) {
	if (!(radius > 0.0f) || !(averageEdgeLength > 0.0f) || !std::isfinite(radius) ||
		!std::isfinite(averageEdgeLength))
		return 1;
	const double rings = std::ceil(static_cast<double>(radius) /
								  static_cast<double>(averageEdgeLength));
	if (rings >= static_cast<double>((std::numeric_limits<int>::max)()))
		return (std::numeric_limits<int>::max)();
	return (std::max)(1, static_cast<int>(rings));
}

namespace {

// Chave unica de (mesh, vertice), para o conjunto dos movidos caber num vetor
// ordenado em vez de num hash.
uint64_t Key(const ContactVertex& vertex) {
	return (static_cast<uint64_t>(static_cast<uint32_t>(vertex.mesh)) << 32) |
		   static_cast<uint32_t>(vertex.vertex);
}

// Devolve nullptr se o indice nao existir. Em producao estes ponteiros vem do
// host, e um indice fora de faixa nao pode virar leitura selvagem.
const Vec3* PositionOf(const std::vector<ContactMesh>& meshes, const ContactVertex& vertex) {
	if (vertex.mesh < 0 || static_cast<size_t>(vertex.mesh) >= meshes.size())
		return nullptr;
	const ContactMesh& mesh = meshes[static_cast<size_t>(vertex.mesh)];
	if (!mesh.positions || vertex.vertex < 0 ||
		static_cast<size_t>(vertex.vertex) >= mesh.vertexCount)
		return nullptr;
	return &mesh.positions[static_cast<size_t>(vertex.vertex)];
}

// Vizinhanca de cada vertice, tirada dos triangulos.
std::vector<std::vector<int>> BuildAdjacency(const ContactMesh& mesh) {
	std::vector<std::vector<int>> neighbours(mesh.vertexCount);
	if (!mesh.triangles)
		return neighbours;
	const auto link = [&neighbours, &mesh](int a, int b) {
		if (a < 0 || b < 0 || a == b || static_cast<size_t>(a) >= mesh.vertexCount ||
			static_cast<size_t>(b) >= mesh.vertexCount)
			return;
		neighbours[static_cast<size_t>(a)].push_back(b);
		neighbours[static_cast<size_t>(b)].push_back(a);
	};
	for (size_t triangle = 0; triangle < mesh.triangleCount; ++triangle) {
		const int* corner = mesh.triangles + triangle * 3;
		link(corner[0], corner[1]);
		link(corner[1], corner[2]);
		link(corner[2], corner[0]);
	}
	for (std::vector<int>& list : neighbours) {
		std::sort(list.begin(), list.end());
		list.erase(std::unique(list.begin(), list.end()), list.end());
	}
	return neighbours;
}

} // namespace

std::vector<ContactCorrection> SolveContacts(const ContactGraph& graph,
											 const std::vector<ContactMesh>& meshes,
											 const std::vector<ContactVertex>& moved,
											 int falloffRings) {
	std::vector<uint64_t> movedKeys;
	movedKeys.reserve(moved.size());
	for (const ContactVertex& vertex : moved)
		movedKeys.push_back(Key(vertex));
	std::sort(movedKeys.begin(), movedKeys.end());
	const auto wasMoved = [&movedKeys](const ContactVertex& vertex) {
		return std::binary_search(movedKeys.begin(), movedKeys.end(), Key(vertex));
	};

	// Ja esta onde deveria: nao vale gastar uma correcao, que em producao vira
	// escrita no host e uma entrada no estado de undo.
	const auto alreadyThere = [](const Vec3& current, const Vec3& target) {
		return DistanceSquared(current, target) <= 1e-10f;
	};

	std::vector<ContactCorrection> corrections;

	// Cluster: todo mundo na posicao de quem se moveu. Se mais de um se moveu, a
	// media dos que se moveram -- e o que da de graca o "estou esculpindo o
	// corpo, a cabeca segue" sem configuracao nenhuma.
	for (const ContactCluster& cluster : graph.clusters) {
		Vec3 sum;
		int count = 0;
		for (const ContactVertex& member : cluster.members) {
			if (!wasMoved(member))
				continue;
			const Vec3* position = PositionOf(meshes, member);
			if (!position)
				continue;
			sum.x += position->x;
			sum.y += position->y;
			sum.z += position->z;
			++count;
		}
		if (count == 0)
			continue;
		const Vec3 target = {sum.x / static_cast<float>(count), sum.y / static_cast<float>(count),
							 sum.z / static_cast<float>(count)};
		for (const ContactVertex& member : cluster.members) {
			const Vec3* position = PositionOf(meshes, member);
			if (position && !alreadyThere(*position, target))
				corrections.push_back({member, target});
		}
	}

	// Vertice em aresta: a razao guardada, reavaliada onde as pontas estao
	// agora. O vertice segue a aresta mesmo quando foi ele que se mexeu -- um
	// ponto sozinho nao determina para onde a aresta vai, entao o lado esparso
	// segue e nao lidera.
	for (const ContactOnEdge& contact : graph.onEdges) {
		if (!wasMoved(contact.follower) && !wasMoved(contact.edgeStart) &&
			!wasMoved(contact.edgeEnd))
			continue;
		const Vec3* follower = PositionOf(meshes, contact.follower);
		const Vec3* start = PositionOf(meshes, contact.edgeStart);
		const Vec3* end = PositionOf(meshes, contact.edgeEnd);
		if (!follower || !start || !end)
			continue;
		const Vec3 target = {start->x + contact.ratio * (end->x - start->x),
							 start->y + contact.ratio * (end->y - start->y),
							 start->z + contact.ratio * (end->z - start->z)};
		if (!alreadyThere(*follower, target))
			corrections.push_back({contact.follower, target});
	}

	if (falloffRings <= 1 || corrections.empty())
		return corrections;

	// Espalha para o interior. Anda de anel em anel a partir dos vertices
	// corrigidos, carregando o deslocamento da fonte mais proxima -- quem chega
	// primeiro na largura fica com ele -- e com o peso caindo a zero no anel de
	// numero falloffRings.
	const size_t ringCorrections = corrections.size();
	for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		const ContactMesh& mesh = meshes[meshIndex];
		if (!mesh.positions || !mesh.triangles)
			continue;

		std::vector<int> depth(mesh.vertexCount, -1);
		std::vector<Vec3> carried(mesh.vertexCount);
		std::vector<int> queue;
		for (size_t i = 0; i < ringCorrections; ++i) {
			const ContactCorrection& correction = corrections[i];
			if (static_cast<size_t>(correction.vertex.mesh) != meshIndex)
				continue;
			const size_t vertex = static_cast<size_t>(correction.vertex.vertex);
			if (vertex >= mesh.vertexCount || depth[vertex] == 0)
				continue;
			const Vec3& from = mesh.positions[vertex];
			depth[vertex] = 0;
			carried[vertex] = {correction.position.x - from.x, correction.position.y - from.y,
							   correction.position.z - from.z};
			queue.push_back(static_cast<int>(vertex));
		}
		if (queue.empty())
			continue;

		const std::vector<std::vector<int>> neighbours = BuildAdjacency(mesh);
		for (size_t head = 0; head < queue.size(); ++head) {
			const size_t vertex = static_cast<size_t>(queue[head]);
			const int nextDepth = depth[vertex] + 1;
			if (nextDepth >= falloffRings) // peso zero: nao vale continuar
				continue;
			for (int neighbour : neighbours[vertex]) {
				if (depth[static_cast<size_t>(neighbour)] != -1)
					continue;
				depth[static_cast<size_t>(neighbour)] = nextDepth;
				carried[static_cast<size_t>(neighbour)] = carried[vertex];
				queue.push_back(neighbour);
			}
		}

		for (size_t vertex = 0; vertex < mesh.vertexCount; ++vertex) {
			if (depth[vertex] < 1)
				continue;
			const float weight =
				1.0f - static_cast<float>(depth[vertex]) / static_cast<float>(falloffRings);
			const Vec3& from = mesh.positions[vertex];
			corrections.push_back({{static_cast<int>(meshIndex), static_cast<int>(vertex)},
								   {from.x + carried[vertex].x * weight,
									from.y + carried[vertex].y * weight,
									from.z + carried[vertex].z * weight}});
		}
	}
	return corrections;
}
