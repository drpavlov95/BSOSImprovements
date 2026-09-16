#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Reordenar sliders arrastando pela alca.
//
// Cada linha do painel de sliders e um painel proprio -- lapis, caixa de
// marcacao, nome, barra e porcentagem dentro dele -- e foi isso que tornou a
// feature possivel: arrastar uma linha e mover UMA janela, nao remontar um
// layout.
//
// A alca e um controle de VERDADE, e nao um enfeite por cima da linha. Ela
// captura o mouse no aperto e so o solta no fim, entao o arrasto nao depende
// de o clique atravessar um controle nem de o hook global adivinhar em que
// janela ele caiu. A primeira versao dependia das duas coisas; funcionava, mas
// por acidente de hit-test.
//
// A ordem vive por NOME de slider, e nao por HWND.
//
// Isso importa porque o wx refaz o layout quando quer -- trocar de outfit,
// redimensionar o painel, filtrar a lista -- e as janelas de ontem nao existem
// mais. Guardar a ordem em coordenadas ou em handles significa perde-la no
// primeiro relayout, que e exatamente o modo como a busca do Symmetrize falhou
// tres vezes antes de aprender a mesma licao.
//
// A ordem vive na tela ate o usuario mandar salvar. Depois do Save/Save As
// nativo, a DLL localiza o unico .osp alterado e o unico SliderSet com a mesma
// lista exata de nomes; so entao move os blocos <Slider> no arquivo.
namespace SliderReorder {

bool Install(HWND frame);
void Uninstall();

// Verdadeiro se a mensagem foi consumida. So o Esc, que cancela um arrasto em
// curso -- o resto do gesto pertence a alca, que capturou o mouse.
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

// A ordem em que as linhas devem aparecer, dada a ordem desejada por nome e os
// nomes que a lista tem AGORA.
//
// O que o usuario escolheu sobrevive a lista ser refeita: os nomes que ele
// ordenou vem primeiro, na ordem dele, e os que apareceram depois -- outro
// outfit, outro projeto -- vao para o fim, na ordem em que o programa os deu.
// Nome que sumiu simplesmente nao entra.
std::vector<int> ApplyDesiredOrder(const std::vector<std::wstring>& desired,
								   const std::vector<std::wstring>& present);

// Costura uma ordem nova de um SUBCONJUNTO de volta na ordem completa.
//
// Existe por causa do filtro de sliders do proprio Outfit Studio. Com ele
// ligado, o usuario arrasta entre as linhas que SOBRARAM na tela, e as
// escondidas nao podem nem saber que houve um arrasto -- elas nao estavam la
// para serem reordenadas.
//
// Os lugares que o subconjunto ocupava trocam de dono; o resto fica parado:
//
//   completa   A B C D E F
//   na tela      B   D   F
//   arrastado    F   B   D
//   resultado  A F C B E D
//
// Substituir a ordem completa pela do subconjunto -- que era o que a versao
// anterior fazia -- jogaria A, C e E para o fim assim que a busca fosse limpa,
// porque nomes fora da ordem desejada vao para o fim por definicao.
std::vector<std::wstring> SpliceOrder(const std::vector<std::wstring>& full,
									  const std::vector<std::wstring>& subsetOrder);
