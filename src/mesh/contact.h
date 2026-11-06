#pragma once

#include <cstddef>
#include <vector>

#include "mesh/obj.h" // Vec3

// Grafo de contato entre meshes.
//
// Responde "quais vertices destas meshes estao se tocando AGORA", e e montado
// uma vez, quando o usuario liga o magnetismo. Congelado de proposito: nada e
// redetectado durante a escultura, senao o trabalho iria criando conexoes novas
// conforme avanca, que nao e o que "se tocando agora" significa.
//
// Nao procura seams. Procura contato: nao importa se o anel e uma borda aberta
// da topologia, importa que um vertice de A ocupa a posicao de um vertice de B,
// ou cai sobre uma aresta de B. FindSeamVertices, em mesh/seams.h, responde a
// pergunta errada -- acha bordas dentro de uma mesh.

// Uma mesh de entrada. Aponta para os arrays de quem chamou, sem copiar: em
// producao eles sao os arrays vivos do host.
struct ContactMesh {
	const Vec3* positions = nullptr;
	size_t vertexCount = 0;
	const int* triangles = nullptr; // tres indices por triangulo
	size_t triangleCount = 0;
};

// Um vertice, identificado pela mesh e pelo indice dentro dela.
struct ContactVertex {
	int mesh = 0;
	int vertex = 0;
};

// Vertices que ocupam a mesma posicao. Cluster, e nao par: tres meshes podem se
// encontrar no mesmo ponto, e um par nao representa isso.
struct ContactCluster {
	std::vector<ContactVertex> members;
};

// Vertice que nao achou par no passe 1 e cai sobre uma aresta de outra mesh.
//
// Guarda a RAZAO ao longo da aresta, e nao a posicao: um snap de posicao faria o
// T-junction deslizar quando a aresta se estica ou gira. A posicao alvo e
// reavaliada como start + ratio * (end - start) conforme as pontas se movem.
//
// A relacao e assimetrica de proposito. Um vertice preso a uma aresta pode
// seguir a aresta; o contrario -- dois vertices seguirem um ponto so -- e mal
// posto. Na pratica: esculpa no lado denso, o lado esparso segue.
struct ContactOnEdge {
	ContactVertex follower;
	ContactVertex edgeStart;
	ContactVertex edgeEnd;
	float ratio = 0.0f; // sempre em (0, 1) aberto: as pontas sao caso do passe 1
};

struct ContactGraph {
	std::vector<ContactCluster> clusters;
	std::vector<ContactOnEdge> onEdges;
};

// Monta o grafo. Dois vertices coincidem quando a distancia entre eles nao passa
// da tolerancia; zero absoluto nao serve em malha vinda de .nif.
ContactGraph BuildContactGraph(const std::vector<ContactMesh>& meshes, float tolerance);

// Onde um vertice tem de estar para o contato continuar de pe.
struct ContactCorrection {
	ContactVertex vertex;
	Vec3 position;
};

// Resolve as restricoes depois que o pincel mexeu nos vertices de `moved`.
//
// Nao escreve nada, de proposito: devolve a lista. Quem chama e que sabe falar
// com o host, e precisa dela inteira -- a correcao so persiste se entrar no
// estado de undo do mesmo traco, junto com o que o pincel ja fez.
//
// `falloffRings` espalha a correcao para o interior da mesh que segue: peso 1 no
// vertice corrigido, caindo a zero no anel de numero `falloffRings`. Mover so o
// anel de contato criaria uma dobra -- e o espalhamento que faz parecer uma
// superficie unica em vez de um anel remendado. 1 nao espalha nada. Quem chama
// amarra esse numero ao raio do pincel.
std::vector<ContactCorrection> SolveContacts(const ContactGraph& graph,
											 const std::vector<ContactMesh>& meshes,
											 const std::vector<ContactVertex>& moved,
											 int falloffRings = 1);

// Comprimento medio de aresta da mesh. E a regua para traduzir o raio do pincel
// em aneis de vizinhanca: uma mesh densa precisa de mais aneis para cobrir a
// mesma distancia.
float AverageEdgeLength(const ContactMesh& mesh);

// Quantos aneis de falloff um pincel de raio `radius` merece numa mesh cujas
// arestas medem `averageEdgeLength`. Nunca menos de um: o contato em si sempre
// e corrigido. Quem chama e que poe um teto.
int RingsForRadius(float radius, float averageEdgeLength);
