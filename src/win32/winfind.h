#pragma once

#include <windows.h>

#include <string>
#include <vector>

std::vector<HWND> ChildrenOf(HWND parent);
std::wstring ClassOf(HWND hwnd);

// nth == 0 devolve a primeira ocorrencia.
HWND FindChildByClass(HWND parent, const wchar_t* cls, int nth = 0);
HWND FindDescendantByClass(HWND root, const wchar_t* cls, int nth = 0);

// Todos os descendentes com essa classe, em profundidade e pre-ordem.
std::vector<HWND> FindDescendantsByClass(HWND root, const wchar_t* cls);

// O maior descendente visivel com essa classe.
//
// Existe porque as janelas que interessam aparecem repetidas: ha mais de um
// wxGLCanvas no processo -- a janela de preview tem o seu -- e a view 3D
// principal e sempre a maior das visiveis.
HWND FindLargestVisibleByClass(HWND root, const wchar_t* cls);

// Verdadeiro se o foco esta num campo de texto editavel. Os atalhos de tecla
// unica precisam disso: sem essa guarda, digitar "R" num filtro viraria atalho.
bool IsTextInputFocused();
