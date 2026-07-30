#pragma once

#include <windows.h>

// Redimensionar o brush estilo Blender: aperta a tecla, move o mouse na
// horizontal, clica para confirmar. Esc ou botao direito cancela.
//
// Aumentar e diminuir o brush sao comandos de menu no Outfit Studio
// (btnIncreaseSize / btnDecreaseSize), entao tudo sai por WM_COMMAND. O outro
// caminho do vanilla -- segurar S e girar a roda -- depende de wxGetKeyState,
// que le o teclado fisico, e exigiria manter S pressionado no sistema inteiro
// via SendInput durante o arrasto.
//
// Nao ha desenho proprio: o Outfit Studio ja desenha o circulo do brush no
// cursor, e o WM_MOUSEMOVE que o usuario gera durante o arrasto redesenha.
namespace BrushResize {

bool Install(HWND frame);
void Uninstall();

bool IsActive();
void Begin(int anchorScreenX);
void OnMouseMove(int screenX);
void Confirm();
void Cancel();

} // namespace BrushResize

// Logica pura, exposta para teste: quantos passos aplicar agora, dado o
// deslocamento acumulado desde o inicio e quantos ja foram aplicados.
// Negativo diminui. Incremental de proposito, para arrastar de volta encolher
// em vez de continuar crescendo.
int StepsToApply(int deltaPixels, float sensitivity, int alreadyApplied);
