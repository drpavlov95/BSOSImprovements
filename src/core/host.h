#pragma once

#include <windows.h>

#include <string>

#include "core/config.h"

enum class HostApp { Unknown, BodySlide, OutfitStudio };

// Exposto para teste.
HostApp DetectApp(const wchar_t* exePath);

// Chamado do DllMain. So guarda o modulo e cria a thread -- nenhum trabalho
// de UI e nenhum LoadLibrary de terceiros dentro do loader lock.
void HostStartup(HMODULE self);
void HostShutdown();

HostApp CurrentApp();
const Config& Cfg();

// Janela principal do wx, ja resolvida pela thread de bootstrap.
HWND MainFrame();

// Pasta do exe, com barra final.
const std::wstring& AppDir();
