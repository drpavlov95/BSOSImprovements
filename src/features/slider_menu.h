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

	// Ids lidos do menu vivo uma unica vez, na instalacao. Reler a cada tecla
	// se mostrou pouco confiavel -- a leitura funcionava na inicializacao e
	// devolvia zero na hora de usar -- e os ids nao mudam durante a sessao.
	UINT importObjId = 0;
	UINT exportObjId = 0;

	bool ok = false;
};

SliderMenuRefs ResolveSliderMenu(const std::wstring& appDir);

// Le e guarda os ids do menu vivo. Chamar uma vez, com o frame ja pronto.
bool BindSliderMenu(HWND frame, SliderMenuRefs& refs);

// O Outfit Studio habilita e desabilita menuImportSlider/menuExportSlider em
// MenuEnterSliderEdit e MenuExitSliderEdit, entao o estado enabled desses
// submenus e o reflexo direto de bEditSlider. Ler o menu evita qualquer
// dependencia de simbolo interno.
bool IsSliderEditModeActive(HWND frame, const SliderMenuRefs& refs);

// Dispara um comando de menu por WM_COMMAND -- o mesmo caminho que o Windows
// usa quando o item e clicado.
bool InvokeMenuCommand(HWND frame, const MenuPath& path);

// Id do comando lido do menu vivo. 0 se o caminho nao existir.
UINT MenuCommandId(HWND frame, const MenuPath& path);

// Dispara por id ja conhecido, sem caminhar o menu.
bool InvokeMenuCommandId(HWND frame, UINT id);

// Despeja no log os caminhos resolvidos, os ids vivos e o estado dos submenus.
// Serve para um unico teste do usuario responder onde a coisa quebrou.
void LogSliderMenuState(HWND frame, const SliderMenuRefs& refs);
