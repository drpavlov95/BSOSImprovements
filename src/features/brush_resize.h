#pragma once

#include <windows.h>

// Redimensionar o brush estilo Blender: aperta a tecla, move o mouse na
// horizontal, clica para confirmar. Esc ou botao direito cancela.
//
// Aumentar e diminuir o brush sao comandos de menu no Outfit Studio
// (btnIncreaseSize / btnDecreaseSize), entao o tamanho sai por WM_COMMAND. O
// outro caminho do vanilla -- segurar S e girar a roda -- depende de
// wxGetKeyState, que le o teclado fisico, e exigiria manter S pressionado no
// sistema inteiro via SendInput durante o arrasto.
namespace BrushResize {

bool Install(HWND frame);
void Uninstall();

bool IsActive();

// anchorScreenX/Y e o ponto onde a tecla foi apertada. O circulo do brush fica
// congelado ali ate o fim do arrasto.
void Begin(int anchorScreenX, int anchorScreenY);

// Aplica o novo tamanho pela distancia horizontal ate a ancora e redesenha.
// O mouse anda livre, como no Blender; quem fica parado e o circulo.
void OnMouseMove(int screenX);

void Confirm();
void Cancel();

} // namespace BrushResize

// Logica pura, exposta para teste: quantos passos aplicar agora, dado o
// deslocamento acumulado desde o inicio e quantos ja foram aplicados.
// Negativo diminui.
int StepsToApply(int deltaPixels, float sensitivity, int alreadyApplied);
