#pragma once

#include <windows.h>

// Zera de uma vez todas as barras do painel de sliders.
//
// Serve nos dois programas, e o painel e achado do mesmo jeito nos dois: e a
// janela com mais barras deslizantes visiveis como filhas diretas, entre as que
// rolam. Nem o BodySlide nem o Outfit Studio tem comando de menu para isso --
// "Clear Slider Data" do Outfit Studio apaga a morfologia do slider, que e
// outra coisa e destrutiva -- entao nao ha comando para disparar, so os
// proprios controles.
namespace ZeroSliders {

bool Install(HWND frame);
void Uninstall();

// Devolve true se a tecla foi tratada: houve um painel para zerar. False deixa
// a tecla seguir para a aplicacao.
bool Run();

} // namespace ZeroSliders

// Logica pura, exposta para teste.

// Para onde uma barra vai ao ser zerada.
//
// Zero, quando zero cabe na faixa dela; senao o extremo mais proximo. As barras
// de slider vao de 0 a 100, mas as de pose vao de -300 a 300, e uma faixa que
// nem passe pelo zero nao pode virar um valor fora do controle.
int ZeroTargetFor(int minimum, int maximum);

// Onde estao as barras que devem ser zeradas, ou nullptr se nao ha painel.
//
// `exclude` sai da disputa: e por onde o painel de pose e mantido fora, porque
// as sete barras dele tambem sao barras e zera-las apagaria a pose do usuario
// em vez dos sliders do projeto.
HWND PickSliderHost(HWND frame, HWND exclude);
