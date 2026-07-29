#pragma once

#include <windows.h>

#include <string>
#include <vector>

std::vector<HWND> ChildrenOf(HWND parent);
std::wstring ClassOf(HWND hwnd);

// nth == 0 devolve a primeira ocorrencia.
HWND FindChildByClass(HWND parent, const wchar_t* cls, int nth = 0);
HWND FindDescendantByClass(HWND root, const wchar_t* cls, int nth = 0);

// Verdadeiro se o foco esta num campo de texto editavel. Os atalhos de tecla
// unica precisam disso: sem essa guarda, digitar "R" num filtro viraria atalho.
bool IsTextInputFocused();
