#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Uma combinacao de tecla. vk == 0 significa "nao configurada / invalida".
struct Hotkey {
	UINT vk = 0;
	bool shift = false;
	bool ctrl = false;
	bool alt = false;

	bool IsValid() const { return vk != 0; }
};

// Uma entrada de [Remap]: liga uma tecla a um comando de menu do Outfit
// Studio, identificado pelo name= dele em res\xrc\OutfitStudio.xrc.
struct RemapEntry {
	std::string xrcName;
	Hotkey key;
};

struct Config {
	bool groupSearch = true;
	bool batchBuildSearch = true;
	bool referenceAutoSelect = true;
	bool sliderObjHotkeys = true;
	bool referenceHotkey = true;
	bool brushResizeDrag = true;

	// 'B' e nao 'R': no vanilla, R e Recalculate Normals. B esta livre e e
	// mnemonico de base shape, que e como o codigo do Outfit Studio chama o
	// reference (project->IsBaseShape). Trocavel em [Hotkeys].
	Hotkey selectReference = Hotkey{'B', false, false, false};
	Hotkey exportSliderObj = Hotkey{'E', true, false, false};
	Hotkey importSliderObj = Hotkey{'I', true, false, false};
	Hotkey brushResize = Hotkey{'F', false, false, false};

	// Passos de brush por pixel de movimento horizontal. O range completo do
	// brush sao 300 passos de 0.010.
	float brushResizeSensitivity = 1.0f;

	std::vector<RemapEntry> remaps;

	bool logFile = false;
};

// Le [Remap]. Entradas com hotkey invalida sao descartadas.
std::vector<RemapEntry> ReadRemapSection(const wchar_t* iniPath);

// Le o INI. Arquivo ausente, secao ausente ou valor malformado caem no default.
// iniPath PRECISA ser absoluto: a API de perfil do Windows resolve caminhos
// relativos contra o diretorio do Windows, nao contra o diretorio atual.
Config LoadConfig(const wchar_t* iniPath);

// Aceita "R", "Shift+E", "Ctrl+Alt+K". Case-insensitive, espacos ignorados.
// Devolve Hotkey invalido se o spec nao fizer sentido.
Hotkey ParseHotkey(const char* spec);
