#pragma once

#include <windows.h>

// Despejo de diagnostico para o log, ligado por [Debug] DumpWindows=1.
//
// Existe porque adivinhar a estrutura da interface saiu caro: features
// inteiras foram construidas em cima de suposicoes sobre classe de controle e
// id de comando que simplesmente nao eram verdade, e o custo so apareceu na
// mao do usuario. Isto troca a suposicao por leitura.
namespace Diag {

// A arvore de janelas a partir de `root`: classe, id, retangulo, visibilidade
// e texto de cada uma.
void DumpWindowTree(HWND root, const char* what);

// Cada barra de ferramentas debaixo de `root`, botao por botao, com id de
// comando e estilo. E o que responde se o id da ferramenta e o mesmo do item
// de menu de mesmo nome.
void DumpToolbars(HWND root);

// Todo item da menubar com id e rotulo cru, para casar contra os ids das
// barras.
void DumpMenuIds(HWND frame);

// Os tres de uma vez, na instalacao.
void DumpEverything(HWND frame);

} // namespace Diag
