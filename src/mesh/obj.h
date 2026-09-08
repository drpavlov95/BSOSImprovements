#pragma once

#include <string>
#include <vector>

// Leitor e escritor de OBJ.
//
// Existe porque o Outfit Studio exporta e importa OBJ pelo proprio menu -- os
// mesmos comandos que o Shift+E e o Shift+I ja disparam. E a unica forma de por
// a malha nas maos deste mod sem ler a memoria do processo, que e o que faria
// tudo aqui quebrar a cada versao nova do BodySlide.
//
// So le o que serve: posicao de vertice e triangulo. Normais e UVs do arquivo
// sao ignoradas -- as normais a gente recalcula, e as UVs voltam intactas
// porque nao mexemos nelas.

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct ObjMesh {
	std::vector<Vec3> positions;

	// Indices base zero, ja triangulados. O OBJ e base UM e aceita face de
	// qualquer tamanho; a conversao acontece na leitura para ninguem depois
	// precisar lembrar disso.
	std::vector<int> triangles; // tres indices por triangulo

	size_t VertexCount() const { return positions.size(); }
	size_t TriangleCount() const { return triangles.size() / 3; }
};

// Le um OBJ. Devolve false se o texto nao tiver vertice nenhum.
//
// A ORDEM dos vertices e preservada, e isso e o contrato inteiro: a mascara e o
// diff de slider do Outfit Studio sao indexados por posicao na lista, entao
// reordenar aqui faria a mascara voltar aplicada nos vertices errados.
bool ParseObj(const std::string& text, ObjMesh& out);

// Escreve um OBJ com a mesma ordem de vertices que entrou.
std::string WriteObj(const ObjMesh& mesh);
