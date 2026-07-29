#pragma once

#include <windows.h>

// Acesso a HMENU por indice de posicao. Indice, e nao texto: o BodySlide inclui
// traducoes para mais de 30 idiomas, entao casar por rotulo quebraria fora do
// ingles. Separadores ocupam posicao.

HMENU SubMenuAt(HMENU parent, int index);
bool IsEnabledAt(HMENU parent, int index);
UINT CommandIdAt(HMENU parent, int index);
