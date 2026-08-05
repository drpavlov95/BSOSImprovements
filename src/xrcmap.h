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

// O caminho mais o rotulo de cada passo, ja normalizado para comparacao:
// sem o acelerador depois do \t, sem os & de mnemonico, sem espaco nas pontas.
//
// So a posicao nao basta. Outros mods inserem itens no menu VIVO, que nao
// existem no XRC -- o mod Drape injeta um separador e "Smart Conform All" no
// menu Slider -- e dali para a frente toda posicao lida do XRC aponta para o
// item errado. Com o rotulo da para reencontrar o item onde quer que ele tenha
// parado, e so cair na posicao quando o texto nao bater (instalacao traduzida).
struct MenuTrail {
	MenuPath path;
	std::vector<std::wstring> labels; // mesmo tamanho de path

	bool empty() const { return path.empty(); }
};

MenuTrail ResolveMenuTrail(const wchar_t* xrcFile, const char* xrcName);
