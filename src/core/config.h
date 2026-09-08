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

// Quais eixos do pose invertem o sinal ao espelhar para o osso do outro lado.
//
// Nao existe resposta unica: depende de como o esqueleto orienta os eixos
// locais de cada osso, e um esqueleto customizado pode discordar do vanilla.
// Por isso sao configuraveis em [MirrorPose]. Os defaults sao a convencao do
// esqueleto do Skyrim, onde X e o eixo que atravessa o corpo de um lado ao
// outro: espelhar nega o deslocamento em X e as duas rotacoes que giram em
// torno dos outros dois eixos.
struct MirrorPoseSigns {
	bool rotationX = false;
	bool rotationY = true;
	bool rotationZ = true;
	bool offsetX = true;
	bool offsetY = false;
	bool offsetZ = false;
	bool scale = false;
};

struct Config {
	bool groupSearch = true;
	bool batchBuildSearch = true;
	bool referenceAutoSelect = true;
	bool sliderObjHotkeys = true;
	bool referenceHotkey = true;
	bool brushResizeDrag = true;
	bool shortcutTooltips = true;
	bool symmetrizeSearch = true;
	bool mirrorBonePose = true;
	bool zeroSlidersHotkey = true;

	// Desligada por padrao, ao contrario de todo o resto: as outras features
	// acrescentam alguma coisa, esta TROCA a navegacao que o usuario ja tem na
	// mao. Quem quer o esquema do Blender liga de proposito.
	bool blenderCamera = false;

	// 'B' e nao 'R': no vanilla, R e Recalculate Normals. B esta livre e e
	// mnemonico de base shape, que e como o codigo do Outfit Studio chama o
	// reference (project->IsBaseShape). Trocavel em [Hotkeys].
	Hotkey selectReference = Hotkey{'B', false, false, false};
	Hotkey exportSliderObj = Hotkey{'E', true, false, false};
	Hotkey importSliderObj = Hotkey{'I', true, false, false};
	Hotkey brushResize = Hotkey{'F', false, false, false};

	// Shift+F e a tecla do Blender para forca, e esta livre no Outfit Studio:
	// os Shift+ que ele usa sao os numeros, R, N, B e os sinais.
	Hotkey brushStrength = Hotkey{'F', true, false, false};

	// Z esta livre nos dois programas: o menu do Outfit Studio so usa Ctrl+Z, e
	// o BodySlide nao tem atalho nenhum.
	Hotkey zeroSliders = Hotkey{'Z', false, false, false};

	// Passos de brush por pixel de movimento horizontal. O range completo do
	// tamanho sao 300 passos de 0.010.
	float brushResizeSensitivity = 1.0f;

	// Quantos passos a forca tem de ponta a ponta.
	//
	// O do tamanho esta no codigo do Outfit Studio e vale 300; o da forca nao
	// aparece em recurso nenhum, entao fica aqui. Errar para MAIS e o que
	// importa: passar do fim gera comandos que o programa ignora, e ai o
	// cancelar aplicaria de volta passos que nunca surtiram efeito, deixando o
	// brush mais fraco do que comecou. Por isso o default e conservador.
	int brushStrengthSteps = 100;

	std::vector<RemapEntry> remaps;

	// [TooltipShortcuts] so ROTULA, nao liga tecla nenhuma. Existe para as
	// teclas que o proprio Outfit Studio trata no codigo e que portanto nao
	// aparecem em lugar nenhum que de para ler de fora -- os numeros que trocam
	// de brush sao o caso tipico.
	std::vector<RemapEntry> tooltipShortcuts;

	MirrorPoseSigns mirrorSigns;

	bool logFile = false;
};

// Le uma secao de linhas "xrcName=tecla". Entradas com hotkey invalida sao
// descartadas: uma linha errada nao pode derrubar as outras.
std::vector<RemapEntry> ReadKeySection(const wchar_t* iniPath, const wchar_t* section);

// Le [Remap]. Atalho para ReadKeySection na secao de sempre.
std::vector<RemapEntry> ReadRemapSection(const wchar_t* iniPath);

// So a chave [Debug]/LogFile, para o log poder ser ligado ANTES de o resto do
// INI ser lido. Sem isto, qualquer diagnostico emitido durante o parsing cai no
// vazio: o log ainda nao existe quando ele acontece.
bool ReadLogFileFlag(const wchar_t* iniPath);

// Le o INI. Arquivo ausente, secao ausente ou valor malformado caem no default.
// iniPath PRECISA ser absoluto: a API de perfil do Windows resolve caminhos
// relativos contra o diretorio do Windows, nao contra o diretorio atual.
Config LoadConfig(const wchar_t* iniPath);

// Aceita "R", "Shift+E", "Ctrl+Alt+K". Case-insensitive, espacos ignorados.
// Devolve Hotkey invalido se o spec nao fizer sentido.
Hotkey ParseHotkey(const char* spec);
