#pragma once

#include <windows.h>

#include <string>

// O BodySlide 5.8.2 ganhou modo claro/escuro (wxApp::SetAppearance). Os dois
// dialogos que este mod desenha sozinho -- o Choose Groups substituto e a faixa
// de busca do Batch Build -- sao Win32 cru e nao herdam nada disso: ficariam
// claros dentro de uma janela escura.
enum class Appearance {
	Light,
	Dark,
};

// A decisao, isolada para teste. `mode` e o valor cru de <AppearanceMode> no
// Config.xml do BodySlide.
//
// Espelha SettingsDialogShared::GetConfiguredAppearance: "Dark" e escuro,
// "System" segue o Windows, e QUALQUER outra coisa -- inclusive a chave
// ausente -- e claro. O default do BodySlide e "Light", nao "System".
Appearance ResolveAppearance(const std::string& mode, bool systemPrefersDark);

// Le HKCU\...\Themes\Personalize\AppsUseLightTheme.
bool SystemPrefersDark();

// Le o <AppearanceMode> do Config.xml ao lado do executavel.
Appearance DetectAppearance(const std::wstring& appDir);

// Cores do modo escuro. Proximas do que o Explorer usa, para o dialogo nao
// destoar do resto da janela.
const COLORREF kDarkBackground = RGB(32, 32, 32);
const COLORREF kDarkControlBackground = RGB(43, 43, 43);
const COLORREF kDarkText = RGB(255, 255, 255);

// Pinceis do modo escuro, com tempo de vida do processo. Devolver um pincel
// recem-criado a cada WM_CTLCOLOR vazaria um objeto GDI por mensagem.
HBRUSH DarkBackgroundBrush();
HBRUSH EditBackgroundBrush();

// Pede ao Windows a variante escura dos controles comuns. Sem efeito quando o
// processo nao esta em modo escuro, entao e seguro chamar sempre.
void ApplyDarkControlTheme(HWND control, bool isEdit);
