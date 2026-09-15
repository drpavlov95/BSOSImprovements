#pragma once

#include <windows.h>

// Navegacao da view 3D no esquema do Blender.
//
// O vanilla dos dois programas, lado a lado:
//
//   Outfit Studio          Blender
//   meio ......... pan     meio ......... orbita
//   direito ...... gira    Shift+meio ... pan
//   Shift+meio ... zoom    Ctrl+meio .... zoom
//   roda ......... zoom    roda ......... zoom
//
// A roda ja e a mesma nos dois. O resto e uma permutacao de tres:
//
//   meio        -> direito     (orbitar)
//   Shift+meio  -> meio        (pan)
//   Ctrl+meio   -> Shift+meio  (zoom)
//
// O botao direito continua girando. Tirar isso nao acrescentaria nada e
// quebraria o habito de quem usa os dois programas.
//
// A mensagem original e reescrita e segue pelo caminho de sempre. Como o
// Outfit Studio consulta o estado fisico do Shift para distinguir pan e zoom,
// esse modificador tambem e temporariamente sintetizado durante o arrasto.
namespace BlenderCamera {

bool Install(HWND frame);
void Uninstall();

bool IsEnabled();
void SetEnabled(bool enabled);

// Reescreve a mensagem no lugar, se for uma de mouse na view 3D. Chamada de
// dentro do unico hook de mensagens do mod.
void RewriteMouseMessage(MSG* msg);

} // namespace BlenderCamera

// Logica pura, exposta para teste.

// O que ja foi traduzido e ainda nao terminou. Um arrasto e decidido no
// aperto: soltar o Shift no meio nao muda orbita para pan.
struct CameraState {
	bool orbiting = false;
	bool panning = false;
	bool zooming = false;
};

// Traduz uma mensagem de mouse do esquema do Blender para o do Outfit Studio.
// Devolve true se mexeu em alguma coisa.
bool TranslateCameraMessage(CameraState& state, UINT& message, WPARAM& wParam);

// Se o arrasto em curso ficou preso: o botao foi solto com o ponteiro fora da
// janela -- alt-tab no meio do movimento -- e o WM_MBUTTONUP foi parar em outro
// processo.
//
// Sem detectar isso o estado ficaria em "orbitando" para sempre, e a partir dali
// TODO movimento de mouse chegaria ao programa como se o botao direito
// estivesse apertado: a camera giraria sozinha sem nada apertado.
//
// So olha WM_MOUSEMOVE de proposito. Logo depois do aperto o estado fisico da
// tecla ainda pode nao ter chegado a este ponto, e um teste cedo demais
// cancelaria o arrasto no instante em que ele comeca.
bool DragWasReleasedOutside(const CameraState& state, UINT message, bool middleDown);
