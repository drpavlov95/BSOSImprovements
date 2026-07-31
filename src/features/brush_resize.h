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

// Aplica o novo tamanho pela distancia horizontal ate a ancora e reescreve a
// mensagem para o app enxergar o cursor parado na ancora.
//
// A mensagem NAO e consumida de proposito. Quem redesenha a cena e o proprio
// tratamento de movimento do Outfit Studio; engolir a mensagem e sintetizar
// outra no lugar mata esse caminho, e foi exatamente o que fez o circulo
// parar de mudar de tamanho na tela.
void RewriteMouseMove(MSG* msg);

void Confirm();
void Cancel();

} // namespace BrushResize

// Logica pura, exposta para teste: quantos passos aplicar agora, dado o
// deslocamento acumulado desde o inicio e quantos ja foram aplicados.
// Negativo diminui.
int StepsToApply(int deltaPixels, float sensitivity, int alreadyApplied);
