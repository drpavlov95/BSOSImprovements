#pragma once

#include <windows.h>

// INSTRUMENTACAO DESCARTAVEL -- nao construa feature sobre este arquivo.
//
// Existe para responder tres perguntas sobre o Outfit Studio 5.8.2 que decidem
// o desenho do magnetismo de seams, e depois ser apagado:
//
//   1. O pincel le Mesh::norms na hora em que age?
//   2. Se le, quando o host recalcula as normais durante o arrasto?
//   3. O que escrevemos em Mesh::verts chega no projeto salvo?
//
// Ligado por [Debug] WeldProbe=1 no INI, desligado por padrao.
namespace WeldProbe {

bool Install(HWND frame);
void Uninstall();

} // namespace WeldProbe
