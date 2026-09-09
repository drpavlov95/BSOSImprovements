#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Caixa de busca no dialogo de "Symmetrize Vertices..." e "Mask Symmetric
// Vertices...", que sao o mesmo dialogo com titulos diferentes.
//
// Ele lista uma linha por slider e uma por osso, e num projeto grande sao
// centenas. A busca esconde as linhas que nao casam.
//
// Esconder, e nao desmarcar: o dialogo age sobre o que esta MARCADO, e o
// estado das marcacoes nunca e tocado aqui. Uma linha escondida pelo filtro
// continua marcada, como no Choose Groups.
//
// A recomposicao da lista e do proprio wx. Um sizer nao reserva espaco para
// janela escondida, entao basta esconder as linhas e provocar um novo layout:
// ele fecha os buracos e refaz a faixa de rolagem sozinho. Fazer a geometria na
// mao daria errado no primeiro rolar da lista.
namespace SymmetrizeSearch {

bool Install(HWND frame);
void Uninstall();

} // namespace SymmetrizeSearch

// Logica pura, exposta para teste.

// Um controle de uma linha, e a que altura ele fica dentro dela.
//
// O deslocamento e guardado porque os controles de uma linha nao estao todos na
// mesma altura: a caixa de marcacao e mais alta que os textos, que ficam
// centralizados nela. Mover tudo para o mesmo topo desalinharia a linha.
struct AsymCell {
	HWND window = nullptr;
	int offsetY = 0;
};

// Uma linha da lista: a caixa de marcacao e tudo que esta na mesma altura que
// ela -- o nome, a media e a contagem.
struct AsymRow {
	HWND check = nullptr;
	HWND host = nullptr;
	std::vector<AsymCell> cells;
	std::wstring name;
	int top = 0;

	// O estado que o filtro deixou. Guardado para so mexer no que mudou entre
	// uma tecla e a seguinte.
	bool visible = true;

	// Linha de cabecalho do dialogo -- "Position", "0 sliders", "26 bones" --
	// em vez de uma linha de slider ou de osso.
	//
	// Elas NUNCA sao escondidas. Escondendo-as, digitar qualquer coisa que nao
	// casasse com elas esvaziava o dialogo inteiro e ele parecia quebrado, que
	// foi exatamente o que aconteceu.
	bool fixed = false;
};

// Agrupa os filhos de um painel em linhas pela altura.
//
// Por altura e nao por ordem de enumeracao: a z-order dos filhos nao promete
// acompanhar a ordem de criacao, e agrupar errado esconderia o nome de uma
// linha junto com a contagem de outra.
std::vector<AsymRow> GroupRowsByTop(HWND parent);

// Para onde cada linha vai depois do filtro.
//
// Recebe os topos originais das linhas, em ordem, e quais delas ficam. Devolve
// um topo por linha: as que ficam ocupam os primeiros lugares, sempre nos
// MESMOS lugares que a lista ja tinha, e as escondidas devolvem o topo que
// tinham -- elas nao sao movidas, so escondidas.
//
// Fechar o buraco na mao, e nao esperar que o wx recomponha: esconder janela
// nao faz o sizer dele refazer o layout, e o resultado era uma lista cheia de
// vazios com os resultados espalhados no meio.
std::vector<int> CompactRowTops(const std::vector<int>& tops, const std::vector<bool>& visible);

// O painel que contem a lista: entre os filhos diretos do dialogo, aquele com
// mais caixas de marcacao dentro. Nullptr se nenhum tiver ao menos duas.
HWND FindAsymScroll(HWND dlg);
