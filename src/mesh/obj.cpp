#include "mesh/obj.h"

#include <cstdio>
#include <cstdlib>

namespace {

// Avanca ate o proximo caractere que nao seja espaco ou tabulacao.
const char* SkipBlanks(const char* p, const char* end) {
	while (p < end && (*p == ' ' || *p == '\t'))
		++p;
	return p;
}

const char* SkipToLineEnd(const char* p, const char* end) {
	while (p < end && *p != '\n' && *p != '\r')
		++p;
	return p;
}

const char* SkipLineBreaks(const char* p, const char* end) {
	while (p < end && (*p == '\n' || *p == '\r'))
		++p;
	return p;
}

// Le um float e devolve onde parou. `ok` fica falso se nao houver numero.
const char* ReadFloat(const char* p, const char* end, float& value, bool& ok) {
	p = SkipBlanks(p, end);

	char* stop = nullptr;
	// strtof para de ler no primeiro caractere que nao serve, entao ele nunca
	// passa do fim da linha -- e o texto termina em nulo.
	value = std::strtof(p, &stop);
	ok = (stop != p) && (stop <= end);
	return ok ? stop : p;
}

// Le o indice de vertice de um vertice de face: "12", "12/3", "12/3/4",
// "12//4". So o primeiro numero interessa aqui.
//
// O OBJ conta a partir de UM, e aceita indice NEGATIVO, que conta de tras para
// frente a partir do ultimo vertice lido ate agora. Ignorar o negativo faria a
// face apontar para o vertice errado num arquivo perfeitamente valido.
const char* ReadFaceIndex(const char* p, const char* end, int vertexCount, int& index, bool& ok) {
	p = SkipBlanks(p, end);

	char* stop = nullptr;
	const long raw = std::strtol(p, &stop, 10);
	if (stop == p) {
		ok = false;
		return p;
	}

	if (raw > 0)
		index = static_cast<int>(raw) - 1;
	else if (raw < 0)
		index = vertexCount + static_cast<int>(raw);
	else {
		ok = false; // zero nao e indice valido em OBJ
		return stop;
	}

	ok = (index >= 0 && index < vertexCount);

	// Consome o resto do grupo -- as barras e os indices de UV e normal.
	p = stop;
	while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
		++p;
	return p;
}

} // namespace

bool ParseObj(const std::string& text, ObjMesh& out) {
	out.positions.clear();
	out.triangles.clear();

	const char* p = text.c_str();
	const char* end = p + text.size();

	std::vector<int> face;

	while (p < end) {
		p = SkipBlanks(p, end);
		if (p >= end)
			break;

		if (p[0] == 'v' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
			const char* line = SkipToLineEnd(p, end);

			Vec3 position;
			bool ok = true;
			bool got = false;
			const char* cursor = p + 1;
			cursor = ReadFloat(cursor, line, position.x, got);
			ok = ok && got;
			cursor = ReadFloat(cursor, line, position.y, got);
			ok = ok && got;
			ReadFloat(cursor, line, position.z, got);
			ok = ok && got;

			// Linha de vertice quebrada e descartada em silencio seria pior que
			// falhar: todo indice depois dela apontaria para o vertice errado.
			if (!ok) {
				out.positions.clear();
				out.triangles.clear();
				return false;
			}

			out.positions.push_back(position);
			p = SkipLineBreaks(line, end);
			continue;
		}

		if (p[0] == 'f' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
			const char* line = SkipToLineEnd(p, end);
			const int vertexCount = static_cast<int>(out.positions.size());

			face.clear();
			const char* cursor = p + 1;
			for (;;) {
				cursor = SkipBlanks(cursor, line);
				if (cursor >= line)
					break;

				int index = 0;
				bool ok = false;
				cursor = ReadFaceIndex(cursor, line, vertexCount, index, ok);
				if (!ok)
					break;
				face.push_back(index);
			}

			// Leque a partir do primeiro vertice: e o que transforma quad e
			// n-gono em triangulos sem mexer na ordem dos vertices.
			for (size_t i = 2; i < face.size(); ++i) {
				out.triangles.push_back(face[0]);
				out.triangles.push_back(face[i - 1]);
				out.triangles.push_back(face[i]);
			}

			p = SkipLineBreaks(line, end);
			continue;
		}

		// Qualquer outra linha -- vt, vn, g, usemtl, comentario -- e ignorada.
		p = SkipLineBreaks(SkipToLineEnd(p, end), end);
	}

	return !out.positions.empty();
}

std::string WriteObj(const ObjMesh& mesh) {
	std::string text;
	// Uma reserva grosseira evita dezenas de realocacoes num corpo de 50 mil
	// vertices.
	text.reserve(mesh.positions.size() * 40 + mesh.triangles.size() * 8);

	char line[128];
	for (const Vec3& position : mesh.positions) {
		// Seis casas: o suficiente para o ida-e-volta nao mexer na malha, e
		// menos que o ruido de um float.
		const int written = std::snprintf(line, sizeof(line), "v %.6f %.6f %.6f\n",
										  static_cast<double>(position.x),
										  static_cast<double>(position.y),
										  static_cast<double>(position.z));
		if (written > 0)
			text.append(line, static_cast<size_t>(written));
	}

	for (size_t i = 0; i + 2 < mesh.triangles.size(); i += 3) {
		const int written = std::snprintf(line, sizeof(line), "f %d %d %d\n",
										  mesh.triangles[i] + 1,
										  mesh.triangles[i + 1] + 1,
										  mesh.triangles[i + 2] + 1);
		if (written > 0)
			text.append(line, static_cast<size_t>(written));
	}

	return text;
}
