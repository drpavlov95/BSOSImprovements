#pragma once

#include <windows.h>

// Executa fn na thread que possui `target`, de forma sincrona.
//
// Existe porque instalar hook e subclasse a partir da thread de bootstrap nao
// funciona: SetWindowSubclass exige a thread dona da janela, e falha em
// silencio quando chamado de fora dela.
//
// Mecanismo: um hook WH_CALLWNDPROC temporario na thread de UI, disparado por
// um SendMessage(WM_NULL). O SendMessage entre threads roda o WndProc -- e o
// hook -- na thread de destino, e bloqueia ate terminar.
bool RunOnUiThread(HWND target, void (*fn)(void*), void* ctx);
