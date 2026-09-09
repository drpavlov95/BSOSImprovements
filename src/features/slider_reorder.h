#pragma once

#include <windows.h>

#include <vector>

// Reordenar sliders arrastando.
//
// Cada linha do painel de sliders e um painel proprio -- lapis, caixa de
// marcacao, nome, barra e porcentagem dentro dele -- e foi isso que tornou a
// feature possivel: arrastar uma linha e mover UMA janela, nao remontar um
// layout.
//
// O arrasto comeca quando o clique cai no FUNDO da linha, e nao num controle
// dela. Clicar no lapis continua entrando em edit mode, na caixa continua
// marcando, na barra continua puxando o valor; so o espaco vazio e o nome
// pegam a linha. Assim nada do que ja funcionava e roubado.
//
// Por enquanto a ordem e so visual: ela vive na tela e no mod, e o arquivo do
// projeto nao e tocado. Escrever no disco sem o usuario mandar salvar seria
// mexer no trabalho dele pelas costas.
namespace SliderReorder {

bool Install(HWND frame);
void Uninstall();

// Verdadeiro se a mensagem foi consumida pelo arrasto e nao deve seguir.
bool HandleMouseMessage(MSG* msg);

} // namespace SliderReorder

// Logica pura, exposta para teste.

// Em qual lugar da lista cai essa altura.
//
// Os lugares sao os que a lista ja tem, e nao uma altura fixa: as linhas do
// Outfit Studio nao tem todas a mesma altura, e supor que tem faria a linha
// arrastada pular de lugar antes da hora.
int SlotAt(const std::vector<int>& slotTops, int y);

// Tira o item de `from` e o enfia em `to`, empurrando o resto.
//
// Nao e uma troca entre dois: numa troca, arrastar a primeira linha ate o fim
// jogaria a ultima para o topo, e o que o usuario espera e que as do meio
// subam um lugar cada.
void MoveInOrder(std::vector<HWND>& order, int from, int to);
