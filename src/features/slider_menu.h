#pragma once

#include <windows.h>

#include <string>

#include "xrcmap.h"

// Caminhos dos comandos de slider no menu do Outfit Studio, resolvidos pelo
// XRC uma vez na inicializacao.
struct SliderMenuRefs {
	MenuPath importSubmenu; // menuImportSlider -- e daqui que se le o edit mode
	MenuPath exportSubmenu; // menuExportSlider
	MenuPath importObjItem; // sliderImportOBJ
	MenuPath exportObjItem; // sliderExportOBJ
	bool ok = false;
};

SliderMenuRefs ResolveSliderMenu(const std::wstring& appDir);

// O Outfit Studio habilita e desabilita menuImportSlider/menuExportSlider em
// MenuEnterSliderEdit e MenuExitSliderEdit, entao o estado enabled desses
// submenus e o reflexo direto de bEditSlider. Ler o menu evita qualquer
// dependencia de simbolo interno.
bool IsSliderEditModeActive(HWND frame, const SliderMenuRefs& refs);

// Dispara um comando de menu por WM_COMMAND -- o mesmo caminho que o Windows
// usa quando o item e clicado.
bool InvokeMenuCommand(HWND frame, const MenuPath& path);
