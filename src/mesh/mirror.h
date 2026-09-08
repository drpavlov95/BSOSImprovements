#pragma once

#include <vector>

#include "mesh/obj.h"

// Quem e o espelho de quem, e como espelhar uma mascara com isso.
//
// O espelho e no eixo X, que e o que atravessa o corpo nos esqueletos do
// Skyrim e do Fallout -- o mesmo eixo do "X Mirror" do proprio Outfit Studio.

// Para cada vertice, o indice do vertice espelhado, ou -1 se ele nao tiver par.
//
// Sem par ficam dois casos, e os dois sao normais: vertice exatamente no plano
// central, que e o proprio espelho de si mesmo e sai emparelhado consigo, e
// vertice de uma malha assimetrica, que nao tem com quem parear.
//
// A tolerancia e em unidades do modelo. Malha nenhuma e simetrica ao bit: o
// exportador arredonda, o modelador move um vertice sem querer, e um par que
// deveria casar fica a um centesimo de distancia.
std::vector<int> PairMirroredVertices(const std::vector<Vec3>& positions, float tolerance);

// Como as duas mascaras se combinam.
enum class MirrorMode {
	// O que estava mascarado continua, e o espelho entra junto. E o que se
	// espera de "espelhar a mascara": voce mascarou um lado e quer os dois.
	Union,

	// So o espelho fica, o original sai. Serve para passar o trabalho de um
	// lado para o outro sem refazer.
	Replace,
};

// Aplica o pareamento a uma mascara.
//
// A mascara do Outfit Studio e suave: cada vertice tem um valor de 0 a 1, e nao
// um liga-desliga. Por isso a uniao e o MAIOR dos dois valores, e nao um "ou"
// logico -- tratar como booleano transformaria uma borda suave num degrau.
//
// Vertice sem par mantem o valor que tinha: apagar o que nao tem espelho
// destruiria a mascara justamente nas partes assimetricas da roupa.
std::vector<float> MirrorMask(const std::vector<float>& mask, const std::vector<int>& pairs,
							  MirrorMode mode);
