#pragma once

#include <windows.h>

// Uma combinacao de tecla. vk == 0 significa "nao configurada / invalida".
struct Hotkey {
	UINT vk = 0;
	bool shift = false;
	bool ctrl = false;
	bool alt = false;

	bool IsValid() const { return vk != 0; }
};

struct Config {
	bool groupSearch = true;
	bool referenceAutoSelect = true;
	bool sliderObjHotkeys = true;
	bool referenceHotkey = true;

	Hotkey selectReference = Hotkey{'R', false, false, false};
	Hotkey exportSliderObj = Hotkey{'E', true, false, false};
	Hotkey importSliderObj = Hotkey{'I', true, false, false};

	bool logFile = false;
};

// Le o INI. Arquivo ausente, secao ausente ou valor malformado caem no default.
// iniPath PRECISA ser absoluto: a API de perfil do Windows resolve caminhos
// relativos contra o diretorio do Windows, nao contra o diretorio atual.
Config LoadConfig(const wchar_t* iniPath);

// Aceita "R", "Shift+E", "Ctrl+Alt+K". Case-insensitive, espacos ignorados.
// Devolve Hotkey invalido se o spec nao fizer sentido.
Hotkey ParseHotkey(const char* spec);
