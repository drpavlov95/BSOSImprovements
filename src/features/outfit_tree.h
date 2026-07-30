#pragma once

#include <windows.h>
#include <commctrl.h>

// Localiza a arvore outfitShapes entre as quatro SysTreeView32 do Outfit Studio
// (shapes, bones, segments, partitions), pelo texto da raiz.
//
// Os quatro roots sao literais nao traduzidos no codigo do Outfit Studio --
// AddRoot("Shapes"), "Bones", "Segments", "Partitions", nenhum envolvido em _().
// Por isso identificar pela raiz e imune a idioma, e nao depende da z-order dos
// controles como "a primeira arvore" dependeria.
HWND FindOutfitShapesTree(HWND frame);

// O shape reference e o item em negrito: o Outfit Studio chama SetItemBold nele
// quando project->IsBaseShape(shape). Devolve nullptr se o projeto nao tiver
// reference.
HTREEITEM FindReferenceItem(HWND tree);

// Seleciona o reference no frame dado. false se nao houver arvore ou reference.
bool SelectReference(HWND frame);
