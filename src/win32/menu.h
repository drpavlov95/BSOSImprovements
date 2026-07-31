#pragma once

#include <windows.h>

#include <vector>

// Acesso a HMENU por indice de posicao. Indice, e nao texto: o BodySlide inclui
// traducoes para mais de 30 idiomas, entao casar por rotulo quebraria fora do
// ingles. Separadores ocupam posicao.

HMENU SubMenuAt(HMENU parent, int index);
bool IsEnabledAt(HMENU parent, int index);
UINT CommandIdAt(HMENU parent, int index);

// Versoes que caminham um caminho posicional inteiro, como o que o xrcmap
// produz. Ex.: {5, 12, 2} = menu 5 da barra, item 12, subitem 2.
// Todas devolvem valor neutro (nullptr / false / 0) se o caminho nao existir.
HMENU SubMenuAtPath(HMENU bar, const std::vector<int>& path);
bool IsEnabledAtPath(HMENU bar, const std::vector<int>& path);
UINT CommandIdAtPath(HMENU bar, const std::vector<int>& path);

// fState cru do item, para diagnostico. 0xFFFFFFFF se o caminho nao existir.
UINT StateAtPath(HMENU bar, const std::vector<int>& path);
