#pragma once

#include <windows.h>

#include <vector>

// Onde ficam os controles de pose do Outfit Studio, achados de fora.
//
// O painel e o "panewindow" do wxCollapsiblePane "posePane". Ele nao tem nome
// nem id estavel em runtime, entao a identificacao e por conteudo: e a unica
// janela do programa com SETE barras deslizantes e SETE campos de texto como
// filhos diretos -- rotacao XYZ, deslocamento XYZ e escala.
//
// O painel de luzes tem barras, mas nao sete com sete campos ao lado; o painel
// de sliders do projeto tem uma barra por slider, numero que varia. Nenhum dos
// dois casa com essa contagem por acidente.
struct PosePanel {
	HWND panel = nullptr;

	// cPoseBone: a lista de ossos. E o wxChoice, nao o wxComboBox de nome de
	// pose -- os dois sao "ComboBox" no Windows, e o que os separa e o estilo:
	// wxChoice vira CBS_DROPDOWNLIST, sem campo de digitacao.
	HWND boneChoice = nullptr;

	// cbPose: a caixa "Show Pose". O mirror usa a sua posicao como ancora para
	// entrar naturalmente na mesma linha, sem criar uma janela solta por cima
	// do painel de pose.
	HWND showPose = nullptr;

	// De cima para baixo, na ordem em que o XRC os empilha:
	// rotacao X, Y, Z, deslocamento X, Y, Z, escala.
	HWND sliders[7] = {};
	HWND texts[7] = {};

	bool ok = false;
};

PosePanel FindPosePanel(HWND frame);

// Uma linha do painel: a barra e o campo de texto que fica ao lado dela.
struct PoseRow {
	HWND slider = nullptr;
	HWND text = nullptr;
	int top = 0;
};

// Ordena as linhas de cima para baixo e casa cada barra com o campo de texto
// da mesma altura.
//
// Exposto para teste, e por ordem vertical e nao por ordem de enumeracao: a
// z-order dos filhos nao promete acompanhar a ordem de criacao, e trocar duas
// linhas aqui significaria escrever a rotacao no campo do deslocamento.
std::vector<PoseRow> PairPoseRows(const std::vector<HWND>& sliders, const std::vector<HWND>& texts);
