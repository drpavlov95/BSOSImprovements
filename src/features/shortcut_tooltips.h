#pragma once

#include <windows.h>

#include <map>
#include <string>

#include "core/config.h"

// Mostra o atalho de teclado no tooltip das ferramentas de barra do Outfit
// Studio: "Shows a transform tool... (K)" em vez de so a descricao.
//
// O atalho nao e inventado aqui. Cada ferramenta de barra tem um item de menu
// com o MESMO name= no XRC -- btnTransform, btnPivot, btnViewFront -- e o wx
// atribui um unico XRCID por nome, entao os dois compartilham o id de comando.
// Basta ler o acelerador do menu vivo e casar pelo id. Isso sobrevive a
// traducao (o acelerador vem depois do \t, fora do texto traduzido) e a
// qualquer versao nova, porque nada aqui e posicao nem endereco.
//
// Por cima disso vem o INI: um [Remap] muda a tecla de verdade, entao o
// tooltip tem que mostrar a nova e nao a que o menu ainda anuncia.
namespace ShortcutTooltips {

bool Install(HWND frame);
void Uninstall();

} // namespace ShortcutTooltips

// Logica pura, exposta para teste.

// O acelerador de um rotulo de menu cru: o que vem depois do \t.
// "Transform\tF" -> "F". Sem \t, ou vazio depois dele -> vazio.
std::wstring AcceleratorFromMenuLabel(const std::wstring& rawLabel);

// "K", "Shift+E", "Ctrl+Alt+K". Hotkey invalida -> vazio.
std::wstring FormatHotkey(const Hotkey& key);

// "Transform tool." + "K" -> "Transform tool. (K)".
//
// Idempotente de proposito: a notificacao de tooltip pode passar por mais de
// uma subclasse nossa antes de chegar ao controle, e sem isso o sufixo sairia
// duplicado.
std::wstring ComposeTooltip(const std::wstring& original, const std::wstring& accel);

// Percorre a menubar inteira e devolve "id de comando -> acelerador" para todo
// item que tenha um.
std::map<UINT, std::wstring> CollectMenuAccelerators(HMENU bar);
