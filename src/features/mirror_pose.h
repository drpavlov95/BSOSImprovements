#pragma once

#include <windows.h>

#include <string>

#include "core/config.h"

// Espelha o pose para o osso do outro lado: mexeu na coxa direita, a esquerda
// acompanha, com o mesmo valor e o sinal invertido nos eixos que espelham.
//
// Tudo passa pela propria interface do Outfit Studio. O painel de pose guarda
// um conjunto de valores por osso, entao espelhar e: ler as sete barras do osso
// atual, trocar a lista para o osso espelhado, escrever la os valores com o
// sinal certo, e voltar a lista para onde estava. O usuario ve a lista piscar
// uma vez por arrasto e nada mais.
//
// Por isso o espelho so acontece no FIM do arrasto, e nao a cada pixel: trocar
// o osso selecionado a cada mensagem faria as barras pularem embaixo do mouse.
namespace MirrorPose {

bool Install(HWND frame);
void Uninstall();

} // namespace MirrorPose

// Logica pura, exposta para teste.

// O nome do osso do outro lado, ou vazio se este osso nao tem lado.
//
// Cobre as convencoes que aparecem nos esqueletos usados na pratica:
// "NPC L Thigh [LThg]", "L Breast01", "LArm", "Thigh_L", "LeftHand". Um osso
// central -- Root, Pelvis, NPC Spine [Spn0] -- devolve vazio, e nada acontece.
std::wstring MirrorBoneName(const std::wstring& name);

// Aplica a tabela de sinais. A ordem e a das barras, de cima para baixo:
// rotacao X, Y, Z, deslocamento X, Y, Z, escala.
void MirrorValues(const int in[7], int out[7], const MirrorPoseSigns& signs);
