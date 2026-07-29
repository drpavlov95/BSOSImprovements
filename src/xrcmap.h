#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Caminho posicional dentro da menubar: {indice do menu de topo, indice do
// item, ...}. Ex.: {5, 12, 2} = 6o menu, 13o item, 3o subitem.
using MenuPath = std::vector<int>;

// Le o .xrc e devolve o caminho posicional do elemento de menu com aquele
// name=. Vazio se nao achou ou se o arquivo nao abriu.
//
// Posicao, e nao texto: o BodySlide distribui traducoes para mais de 30
// idiomas, entao casar por rotulo quebraria fora do ingles. Tambem nao serve
// casar por ID: XRCID() e atribuido em runtime pela ordem de registro, entao
// nao ha valor estavel para hardcodar.
MenuPath ResolveMenuPath(const wchar_t* xrcFile, const char* xrcName);
