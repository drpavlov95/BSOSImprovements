#pragma once

#include <windows.h>

#include <string>

#include "xrcmap.h"

// Caminhos dos comandos de slider no menu do Outfit Studio, resolvidos pelo
// XRC uma vez na inicializacao.
struct SliderMenuRefs {
	MenuTrail importSubmenu; // menuImportSlider -- e daqui que se le o edit mode
	MenuTrail exportSubmenu; // menuExportSlider
	MenuTrail importObjItem; // sliderImportOBJ
	MenuTrail exportObjItem; // sliderExportOBJ

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
bool InvokeMenuCommand(HWND frame, const MenuTrail& trail);

// Id do comando lido do menu vivo. 0 se o caminho nao existir.
UINT MenuCommandId(HWND frame, const MenuTrail& trail);

// Dispara por id ja conhecido, sem caminhar o menu. Assincrono: a mensagem vai
// para a fila. Use para comandos que abrem dialogo, que nao podem bloquear
// dentro de um hook.
bool InvokeMenuCommandId(HWND frame, UINT id);

// Versao sincrona: o comando termina antes de retornar.
//
// Necessaria quando algo precisa acontecer DEPOIS do efeito do comando -- caso
// do brush, em que redesenhar logo apos um PostMessage mostraria o tamanho
// antigo, porque o comando ainda estaria na fila.
bool SendMenuCommandId(HWND frame, UINT id);

// Despeja no log os caminhos resolvidos, os ids vivos e o estado dos submenus.
// Serve para um unico teste do usuario responder onde a coisa quebrou.
void LogSliderMenuState(HWND frame, const SliderMenuRefs& refs);
