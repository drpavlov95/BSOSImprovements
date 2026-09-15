#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Caixa de busca no dialogo de "Symmetrize Vertices..." e "Mask Symmetric
// Vertices...", que sao o mesmo dialogo com titulos diferentes.
//
// Ele lista uma linha por slider e uma por osso, e num projeto grande sao
// centenas. A busca mostra so as que casam.
//
// A lista original NAO e tocada. Isso e o contrario do que esta feature tentou
// fazer durante muito tempo, e a mudanca veio de ler o Actions.xrc do proprio
// programa:
//
//   <object class="wxScrolledWindow" name="asymScroll">
//     <style>wxVSCROLL</style>
//     <scrollrate>5,5</scrollrate>
//     <object class="wxBoxSizer"> ... </object>
//
// Aquela lista e um wxBoxSizer dentro de um wxScrolledWindow. O sizer e dono da
// posicao de cada linha, da altura de cada grupo recolhivel e do tamanho virtual
// da area que rola; a barra anda em unidades de CINCO pixels, e nao em pixels. E
// ele recalcula tudo isso a cada Layout() -- ao expandir um grupo, ao
// redimensionar, ao rolar.
//
// Esconder uma janela por ShowWindow nao e o mesmo que wxWindow::Show(false): o
// sizer continua contando a linha escondida. Entao qualquer geometria que este
// mod escrevesse ali vivia ate o proximo Layout() e sumia. As tentativas foram
// todas variacoes disso -- compactar linhas, encolher paineis, empurrar irmaos,
// reescrever a barra em pixels, conferir e reaplicar -- e todas perderam, porque
// nao ha como dizer ao sizer, de fora, que uma linha saiu do layout.
//
// Entao a busca parou de disputar. Com texto na caixa, uma lista de resultados
// propria -- um ListView comum, nosso -- aparece POR CIMA da area original, com
// so o que casa. Marcar um resultado manda o clique para a caixa de marcacao
// VERDADEIRA, entao o programa recalcula "Vertices that will be symmetrized"
// sozinho e o botao Symmetrize age sobre o que o usuario escolheu. Apagar a
// busca esconde a lista de resultados, e a original reaparece intacta -- porque
// nunca foi mexida.
namespace SymmetrizeSearch {

bool Install(HWND frame);
void Uninstall();

} // namespace SymmetrizeSearch

// Logica pura, exposta para teste.

// Uma linha da lista: a caixa de marcacao e tudo que esta na mesma altura que
// ela -- o nome, a media e a contagem.
struct AsymRow {
	HWND check = nullptr;
	HWND host = nullptr;
	std::vector<HWND> cells;

	// O texto da linha inteira, que e contra o que a busca casa.
	std::wstring name;

	int top = 0;

	// Linha de cabecalho do dialogo -- "Position", "114 sliders", "26 bones".
	//
	// Elas sao agregados: marcam varios de uma vez, e o XRC declara as duas
	// ultimas com wxCHK_3STATE. Ficam sempre no topo da lista de resultados.
	bool fixed = false;
};

// Agrupa os filhos de um painel em linhas pela altura.
//
// Por altura e nao por ordem de enumeracao: a z-order dos filhos nao promete
// acompanhar a ordem de criacao, e agrupar errado juntaria o nome de uma linha
// com a contagem de outra.
std::vector<AsymRow> GroupRowsByTop(HWND parent);

// Poe as linhas agregadas antes das linhas individuais, preservando a ordem
// relativa dentro dos dois grupos.
void PutFixedAsymRowsFirst(std::vector<AsymRow>& rows);

// Leva um checkbox verdadeiro ao estado pedido e envia ao pai o mesmo
// BN_CLICKED produzido por um clique. Exposto para testar inclusive os
// checkboxes de tres estados usados nos agregados.
bool ActivateAsymCheck(HWND check, bool checked);

// A area que rola, entre os descendentes do dialogo: a janela MAIS FUNDA que
// ainda carrega todas as caixas de marcacao. Nullptr se nenhuma tiver ao menos
// duas.
//
// Mais funda, e nao a primeira com muitas: a lista mora dentro de um
// wxStaticBox, e a moldura e a area que rola contam exatamente as mesmas
// linhas, porque uma esta dentro da outra.
HWND FindAsymScroll(HWND dlg);
