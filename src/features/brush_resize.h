#pragma once

#include <windows.h>

// Ajustar o brush arrastando, estilo Blender: aperta a tecla, move o mouse na
// horizontal, clica para confirmar. Esc ou botao direito cancela.
//
// Duas grandezas pelo mesmo mecanismo, como no Blender: F muda o TAMANHO e
// Shift+F muda a FORCA.
//
// Aumentar e diminuir sao comandos de menu no Outfit Studio (btnIncreaseSize /
// btnDecreaseSize, btnIncreaseStr / btnDecreaseStr), entao o valor sai por
// WM_COMMAND. O outro caminho do vanilla -- segurar S e girar a roda -- depende
// de wxGetKeyState, que le o teclado fisico, e exigiria manter S pressionado no
// sistema inteiro via SendInput durante o arrasto.
namespace BrushResize {

// Qual grandeza o arrasto em curso esta mexendo.
enum class Target {
	Size,
	Strength,
};

bool Install(HWND frame);
void Uninstall();

bool IsActive();

// anchorScreenX/Y e o ponto onde a tecla foi apertada. O circulo do brush fica
// congelado ali ate o fim do arrasto.
void Begin(int anchorScreenX, int anchorScreenY, Target target);

// Verdadeiro se o comando dessa grandeza foi resolvido na instalacao. Sem ele
// nao adianta ligar a tecla: o arrasto nao teria o que mandar.
bool Supports(Target target);

// Aplica o novo valor pela distancia horizontal ate a ancora e reescreve a
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
// deslocamento acumulado desde o inicio, quantos ja foram aplicados e quantos
// passos a grandeza tem no total. Negativo diminui.
//
// O teto importa e nao e cosmetico: passar dele so gera comandos que o Outfit
// Studio ignora, e ai o cancelar aplicaria de volta passos que nunca surtiram
// efeito -- o brush terminaria mais fraco, ou menor, do que comecou.
int StepsToApply(int deltaPixels, float sensitivity, int alreadyApplied, int maxSteps);
