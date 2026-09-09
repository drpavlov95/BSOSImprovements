#pragma once

#include <windows.h>

// Itens marcaveis acrescentados aos menus do proprio programa.
//
// Uma feature que muda como o programa responde precisa de um interruptor onde
// o usuario vai procurar, e nao so de uma linha no INI. O menu View e onde
// isso mora.
//
// O modulo existe porque ha mais de um: a camera do Blender e as alcas de
// arrastar slider. Duas copias do mesmo cuidado -- achar id livre, subclassar o
// frame, tirar o separador junto na saida -- e uma copia a mais do que se pode
// manter certa.
namespace MenuToggle {

using Callback = void (*)(bool checked);

// O HMENU de um menu do programa, achado pelo name= dele no XRC -- "menuView",
// por exemplo. Nullptr se nao resolver.
HMENU FindMenu(HWND frame, const char* xrcName);

// Acrescenta um item ao fim do menu, com separador antes se for o primeiro.
// Devolve o id do comando, ou 0 se nao der.
//
// O id nao pode ser um numero fixo escolhido na fe: os ids do wx sao dados em
// runtime pela ordem de registro do XRC, entao nao ha faixa que se possa supor
// livre. Ele e procurado contra a menubar viva.
UINT Add(HWND frame, HMENU menu, const wchar_t* label, bool checked, Callback onToggle);

// Marca ou desmarca, sem disparar o callback.
void SetChecked(UINT id, bool checked);

// Tira todos os itens que este modulo pos, e o separador junto.
void RemoveAll();

} // namespace MenuToggle
