#pragma once

#include <windows.h>

// Estabilizador de traco, como o "Stabilize Stroke" do Blender.
//
// O pincel fica preso ao cursor por uma corda de raio fixo. Enquanto o cursor
// se mexe DENTRO desse raio, o pincel nao anda -- e ai que o tremor da mao
// morre, porque tremor e justamente movimento pequeno. Quando o cursor passa do
// raio, ele arrasta o pincel, que segue sempre a um raio de distancia.
//
// Nada e sintetizado: a mensagem de movimento e reescrita com a posicao do
// pincel e segue pelo caminho de sempre, o mesmo cuidado do resize de brush.
//
// So age durante um traco -- botao esquerdo apertado sobre a view 3D -- e so
// quando a ferramenta na mao e um pincel. Com a ferramenta de selecao o botao
// esquerdo mexe a camera, e atrasar isso seria um defeito, nao uma ajuda.
namespace StrokeStabilizer {

bool Install(HWND frame);
void Uninstall();

bool IsEnabled();

// Reescreve a mensagem no lugar, se ela for parte de um traco. Chamada de
// dentro do unico hook de mensagens do mod.
void RewriteStrokeMessage(MSG* msg);

} // namespace StrokeStabilizer

// Logica pura, exposta para teste.

// A posicao do pincel depois de um movimento do cursor.
//
// Raio zero ou negativo devolve o proprio cursor, que e o estabilizador
// desligado -- e o que faz "raio 0" no INI significar exatamente isso.
POINT StabilizeStroke(POINT brush, POINT cursor, int radius);
