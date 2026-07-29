#pragma once

#include <windows.h>
#include <commctrl.h>

#include <vector>

// Helpers para SysTreeView32. O Outfit Studio usa wxTreeCtrl, que no MSW e
// exatamente esse controle nativo -- da para ler e escrever por mensagem sem
// tocar em nenhuma estrutura interna do wxWidgets.

std::vector<HTREEITEM> ChildItems(HWND tree, HTREEITEM parent);

// wxTreeCtrl::SetItemBold vira TVM_SETITEM com TVIS_BOLD, entao o negrito que o
// Outfit Studio aplica no shape reference e legivel nativamente.
bool IsBold(HWND tree, HTREEITEM item);

// Busca em profundidade. Devolve nullptr se nenhum item estiver em negrito --
// projeto sem reference precisa manter o comportamento original.
HTREEITEM FindBoldDescendant(HWND tree);

// TVM_SELECTITEM/TVGN_CARET: gera TVN_SELCHANGED, entao o wx reage como se o
// usuario tivesse clicado.
void SelectItem(HWND tree, HTREEITEM item);
