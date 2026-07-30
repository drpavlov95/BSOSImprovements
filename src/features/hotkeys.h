#pragma once

#include <windows.h>

#include "core/config.h"

// Um unico hook WH_GETMESSAGE para todos os atalhos do mod. Duas features de
// tecla nao instalam dois hooks.
namespace Hotkeys {

bool Install(HWND frame);
void Uninstall();

} // namespace Hotkeys

// Exposto para teste. Modificadores precisam casar EXATAMENTE: Shift+E nao pode
// disparar com Ctrl+Shift+E, senao roubariamos combinacoes de outros comandos.
bool HotkeyMatches(const Hotkey& hk, UINT vk, bool shift, bool ctrl, bool alt);
