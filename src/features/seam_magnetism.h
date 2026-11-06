#pragma once

#include <windows.h>

namespace SeamMagnetism {

// Magnetismo de seams: os vertices que estao se tocando nao se separam enquanto
// o usuario esculpe.
//
// A correcao acontece DENTRO do traco do pincel, e nao depois dele. E o unico
// jeito de ela persistir e de o Ctrl+Z desfazer a pincelada junto com a
// correcao: o que vira diff do projeto nao e o array de vertices, e o par de
// mapas de inicio e fim que o pincel do host preenche a cada traco. Corrigir
// por fora desfaria o traco e nao a correcao, reabrindo o seam.
//
// Preso por hash as builds do Outfit Studio 5.8.2 e 5.9.1, como as outras
// features nativas. Em qualquer outra, Enable falha e nada e tocado.

// Arma o magnetismo. O grafo de contato e congelado no primeiro traco que vier
// depois disto, montado a partir das meshes daquele traco -- que sao as que o
// usuario tem selecionadas. Dali em diante nada e redetectado: esculpir nao
// cria contatos novos.
//
// `tolerance` e a distancia maxima para dois vertices contarem como em contato;
// zero absoluto nao funciona em malha vinda de .nif. `falloffRings` espalha a
// correcao para o interior da mesh que segue, para o anel corrigido nao virar
// uma dobra.
bool Enable(float tolerance, int falloffRings);

// Desarma, desinstala o hook e joga o grafo fora.
void Disable();

bool IsEnabled();

// Poe o item de menu em Shape, sempre desmarcado. Enable so acontece quando o
// usuario clica nele.
bool Install(HWND frame, float tolerance, int falloffRings);
void Uninstall();

} // namespace SeamMagnetism
