#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Acesso a HMENU por indice de posicao. Separadores ocupam posicao.

HMENU SubMenuAt(HMENU parent, int index);
bool IsEnabledAt(HMENU parent, int index);
UINT CommandIdAt(HMENU parent, int index);

// Versoes que caminham um caminho posicional inteiro, como o que o xrcmap
// produz. Ex.: {5, 12, 2} = menu 5 da barra, item 12, subitem 2.
// Todas devolvem valor neutro (nullptr / false / 0) se o caminho nao existir.
HMENU SubMenuAtPath(HMENU bar, const std::vector<int>& path);
bool IsEnabledAtPath(HMENU bar, const std::vector<int>& path);
UINT CommandIdAtPath(HMENU bar, const std::vector<int>& path);

// fState cru do item, para diagnostico. 0xFFFFFFFF se o caminho nao existir.
UINT StateAtPath(HMENU bar, const std::vector<int>& path);

// Texto do item, ja normalizado: sem o acelerador depois do \t, sem os & de
// mnemonico, sem espaco nas pontas. Vazio para separador.
std::wstring MenuTextAt(HMENU parent, int index);

// As mesmas travessias, mas casando o rotulo antes de usar a posicao.
//
// A posicao do XRC vale enquanto ninguem mexer no menu. Um outro mod que insira
// item em runtime -- o Drape injeta "Smart Conform All" no menu Slider -- empurra
// tudo que vem depois, e a posicao lida do XRC passa a apontar para o item
// errado. Casando pelo rotulo o item e reencontrado onde quer que ele esteja.
//
// `labels` acompanha `path` passo a passo. Rotulo vazio, ambiguo ou que nao
// exista no menu vivo cai na posicao -- que e o comportamento antigo, e o que
// mantem isso funcionando numa instalacao traduzida, onde o menu vivo esta em
// outro idioma e o XRC continua em ingles.
HMENU ContainerAtLabeledPath(HMENU bar, const std::vector<int>& path,
							 const std::vector<std::wstring>& labels, int& outIndex);
bool IsEnabledAtLabeledPath(HMENU bar, const std::vector<int>& path,
							const std::vector<std::wstring>& labels);
UINT CommandIdAtLabeledPath(HMENU bar, const std::vector<int>& path,
							const std::vector<std::wstring>& labels);
