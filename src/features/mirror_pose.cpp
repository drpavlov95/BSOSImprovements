#include "features/mirror_pose.h"

#include <commctrl.h>

#include <vector>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/pose_panel.h"
#include "features/registry.h"

namespace {

const int kPoseRows = 7;
const UINT_PTR kSubclassId = 0xB50D;

PosePanel g_pose;
HWND g_frame = nullptr;
bool g_installed = false;

// Enquanto escrevemos no painel, as nossas proprias mensagens voltam pela
// subclasse. Sem esta trava o espelho dispararia o espelho, e o segundo
// espelharia de volta para o primeiro osso -- um vai e volta infinito com a
// interface travada no meio.
bool g_applying = false;

// Para nao repetir o mesmo aviso a cada arrasto num osso central.
std::wstring g_lastUnmirrorable;

bool IsUpper(wchar_t c) {
	return c >= L'A' && c <= L'Z';
}

bool IsDigit(wchar_t c) {
	return c >= L'0' && c <= L'9';
}

wchar_t FlipSide(wchar_t c) {
	switch (c) {
		case L'L': return L'R';
		case L'R': return L'L';
		case L'l': return L'r';
		case L'r': return L'l';
		default: return 0;
	}
}

bool EqualsAny(const std::wstring& token, const wchar_t* a, const wchar_t* b) {
	return token == a || token == b;
}

// Troca o lado de UM token. Devolve false se o token nao tem lado.
bool FlipToken(std::wstring& token) {
	if (token.empty())
		return false;

	// "L" / "R" soltos: "NPC L Thigh".
	if (token.size() == 1) {
		const wchar_t flipped = FlipSide(token[0]);
		if (!flipped)
			return false;
		token[0] = flipped;
		return true;
	}

	// "Left" / "Right" por extenso.
	if (EqualsAny(token, L"Left", L"LEFT") || token == L"left") {
		const bool upper = (token == L"LEFT");
		token = upper ? L"RIGHT" : (token == L"left" ? L"right" : L"Right");
		return true;
	}
	if (EqualsAny(token, L"Right", L"RIGHT") || token == L"right") {
		const bool upper = (token == L"RIGHT");
		token = upper ? L"LEFT" : (token == L"right" ? L"left" : L"Left");
		return true;
	}

	// "[LThg]" -- o apelido entre colchetes que o esqueleto do Skyrim usa.
	//
	// A letra seguinte tem que ser maiuscula ou digito. Sem essa exigencia,
	// "[Root]" viraria "[Loot]" e o osso central passaria a parecer que tem
	// lado.
	if (token[0] == L'[' && token.size() > 3) {
		const wchar_t flipped = FlipSide(token[1]);
		if (flipped && (IsUpper(token[2]) || IsDigit(token[2]))) {
			token[1] = flipped;
			return true;
		}
	}

	// "Thigh_L", "Thigh.R", "Thigh-L" -- convencao de quem exporta do Blender.
	if (token.size() >= 3) {
		const size_t last = token.size() - 1;
		const wchar_t separator = token[last - 1];
		if (separator == L'_' || separator == L'.' || separator == L'-') {
			const wchar_t flipped = FlipSide(token[last]);
			if (flipped) {
				token[last] = flipped;
				return true;
			}
		}
	}

	// "NPC_L_Thigh" -- o mesmo nome sem espacos.
	const size_t middle = token.find(L"_L_");
	if (middle != std::wstring::npos) {
		token[middle + 1] = L'R';
		return true;
	}
	const size_t middleR = token.find(L"_R_");
	if (middleR != std::wstring::npos) {
		token[middleR + 1] = L'L';
		return true;
	}

	// "LArm", "RThigh", "L01": lado colado no comeco, seguido de maiuscula ou
	// digito. A exigencia de maiuscula e o que impede "Root" de virar "Loot".
	const wchar_t flipped = FlipSide(token[0]);
	if (flipped && (IsUpper(token[1]) || IsDigit(token[1]))) {
		token[0] = flipped;
		return true;
	}

	return false;
}

std::vector<std::wstring> SplitOnSpaces(const std::wstring& text) {
	std::vector<std::wstring> tokens;
	std::wstring current;
	for (wchar_t c : text) {
		if (c == L' ') {
			tokens.push_back(current);
			current.clear();
		} else {
			current.push_back(c);
		}
	}
	tokens.push_back(current);
	return tokens;
}

std::wstring ComboText(HWND combo, int index) {
	const int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0));
	if (len <= 0 || len > 4096)
		return std::wstring();

	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	const int written = static_cast<int>(
		SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data())));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

// Troca o osso selecionado E avisa o dono da lista, senao o Outfit Studio nao
// fica sabendo: CB_SETCURSEL sozinho mexe no controle e nao dispara evento
// nenhum, entao as barras continuariam mostrando o osso anterior.
void SelectBone(int index) {
	SendMessageW(g_pose.boneChoice, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
	SendMessageW(g_pose.panel, WM_COMMAND,
				 MAKEWPARAM(GetDlgCtrlID(g_pose.boneChoice), CBN_SELCHANGE),
				 reinterpret_cast<LPARAM>(g_pose.boneChoice));
}

// Escreve numa barra pelo mesmo caminho que um arrasto de verdade percorre: a
// posicao entra no controle e o dono recebe o WM_HSCROLL. O campo de texto ao
// lado nao e tocado de proposito -- quem o preenche e o proprio Outfit Studio
// ao tratar o scroll, e escrever la tambem so criaria um segundo caminho para
// o mesmo valor.
void WriteSlider(HWND slider, int value) {
	SendMessageW(slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(value));
	SendMessageW(g_pose.panel, WM_HSCROLL,
				 MAKEWPARAM(TB_THUMBPOSITION, static_cast<WORD>(value)),
				 reinterpret_cast<LPARAM>(slider));
	SendMessageW(g_pose.panel, WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0),
				 reinterpret_cast<LPARAM>(slider));
}

// Reordena as sete linhas a partir da posicao ATUAL delas.
//
// A ordem nao pode vir da instalacao. Naquele momento o painel de pose ainda
// esta escondido e pode nem ter sido posicionado pelo wx, e sete barras
// empilhadas no mesmo lugar sairiam em ordem arbitraria -- o que significaria
// escrever a rotacao X no deslocamento Z. Aqui o painel esta na tela, e a
// geometria e confiavel.
//
// Devolve false se as linhas ainda nao estiverem separadas, e ai nada e feito.
bool CurrentRowOrder(HWND ordered[kPoseRows]) {
	std::vector<HWND> sliders(g_pose.sliders, g_pose.sliders + kPoseRows);
	std::vector<HWND> texts(g_pose.texts, g_pose.texts + kPoseRows);

	const std::vector<PoseRow> rows = PairPoseRows(sliders, texts);
	if (rows.size() != kPoseRows)
		return false;

	for (int i = 1; i < kPoseRows; ++i) {
		if (rows[static_cast<size_t>(i - 1)].top >= rows[static_cast<size_t>(i)].top) {
			LogF("mirror pose: as sete linhas nao estao separadas na tela, nao da para saber a ordem");
			return false;
		}
	}

	for (int i = 0; i < kPoseRows; ++i)
		ordered[i] = rows[static_cast<size_t>(i)].slider;
	return true;
}

void ApplyMirror() {
	if (g_applying || !g_pose.ok)
		return;

	// Fora do modo de pose o painel esta escondido, e ai nao ha nada para
	// espelhar -- as barras que sobraram na tela sao de outro painel.
	if (!IsWindowVisible(g_pose.panel))
		return;

	HWND ordered[kPoseRows] = {};
	if (!CurrentRowOrder(ordered))
		return;

	const int current = static_cast<int>(SendMessageW(g_pose.boneChoice, CB_GETCURSEL, 0, 0));
	if (current < 0)
		return;

	const std::wstring bone = ComboText(g_pose.boneChoice, current);
	const std::wstring mirrored = MirrorBoneName(bone);
	if (mirrored.empty()) {
		if (g_lastUnmirrorable != bone) {
			g_lastUnmirrorable = bone;
			LogF("mirror pose: '%ls' nao tem lado, nada a espelhar", bone.c_str());
		}
		return;
	}

	const int target = static_cast<int>(SendMessageW(g_pose.boneChoice, CB_FINDSTRINGEXACT,
													static_cast<WPARAM>(-1),
													reinterpret_cast<LPARAM>(mirrored.c_str())));
	if (target < 0) {
		if (g_lastUnmirrorable != bone) {
			g_lastUnmirrorable = bone;
			LogF("mirror pose: '%ls' espelharia em '%ls', que nao esta na lista de ossos",
				 bone.c_str(), mirrored.c_str());
		}
		return;
	}

	int values[kPoseRows] = {};
	for (int i = 0; i < kPoseRows; ++i)
		values[i] = static_cast<int>(SendMessageW(ordered[i], TBM_GETPOS, 0, 0));

	int flipped[kPoseRows] = {};
	MirrorValues(values, flipped, Cfg().mirrorSigns);

	g_applying = true;
	SelectBone(target);
	for (int i = 0; i < kPoseRows; ++i)
		WriteSlider(ordered[i], flipped[i]);
	SelectBone(current);
	g_applying = false;

	g_lastUnmirrorable.clear();
	LogF("mirror pose: '%ls' -> '%ls' (rot %d,%d,%d desl %d,%d,%d escala %d)",
		 bone.c_str(), mirrored.c_str(), flipped[0], flipped[1], flipped[2],
		 flipped[3], flipped[4], flipped[5], flipped[6]);
}

bool IsPoseSlider(HWND control) {
	for (int i = 0; i < kPoseRows; ++i) {
		if (g_pose.sliders[i] == control)
			return true;
	}
	return false;
}

bool IsPoseText(HWND control) {
	for (int i = 0; i < kPoseRows; ++i) {
		if (g_pose.texts[i] == control)
			return true;
	}
	return false;
}

LRESULT CALLBACK PanelSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, PanelSubclassProc, id);
		g_pose = PosePanel();
	}

	// Deixa o Outfit Studio tratar primeiro: o espelho tem que partir do valor
	// que ele ja aplicou, nao do que estava antes da mensagem.
	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

	if (g_applying)
		return result;

	// TB_ENDTRACK e o fim do arrasto -- e tambem o que chega depois de um
	// clique na regua ou de uma seta do teclado. Espelhar em TB_THUMBTRACK
	// faria a lista de ossos trocar centenas de vezes durante um arrasto.
	if (msg == WM_HSCROLL && LOWORD(wParam) == TB_ENDTRACK &&
		IsPoseSlider(reinterpret_cast<HWND>(lParam))) {
		ApplyMirror();
	}
	// Quem digita o valor no campo em vez de arrastar sai dele quando termina.
	else if (msg == WM_COMMAND && HIWORD(wParam) == EN_KILLFOCUS &&
			 IsPoseText(reinterpret_cast<HWND>(lParam))) {
		ApplyMirror();
	}

	return result;
}

void SubclassHere(void*) {
	if (!SetWindowSubclass(g_pose.panel, PanelSubclassProc, kSubclassId, 0))
		LogF("mirror pose: nao consegui subclassar o painel");
}

void UnsubclassHere(void*) {
	if (g_pose.panel && IsWindow(g_pose.panel))
		RemoveWindowSubclass(g_pose.panel, PanelSubclassProc, kSubclassId);
}

bool Enabled(const Config& cfg) {
	return cfg.mirrorBonePose;
}

} // namespace

std::wstring MirrorBoneName(const std::wstring& name) {
	if (name.empty())
		return std::wstring();

	std::vector<std::wstring> tokens = SplitOnSpaces(name);
	bool changed = false;
	for (std::wstring& token : tokens)
		changed = FlipToken(token) || changed;

	if (!changed)
		return std::wstring();

	std::wstring out;
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (i)
			out.push_back(L' ');
		out += tokens[i];
	}
	return out;
}

void MirrorValues(const int in[7], int out[7], const MirrorPoseSigns& signs) {
	const bool negate[7] = {
		signs.rotationX, signs.rotationY, signs.rotationZ,
		signs.offsetX, signs.offsetY, signs.offsetZ,
		signs.scale,
	};
	for (int i = 0; i < 7; ++i)
		out[i] = negate[i] ? -in[i] : in[i];
}

namespace MirrorPose {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	// O painel de pose so existe depois que um projeto e carregado, e comeca
	// escondido. Procurar agora e o suficiente porque as janelas do wx sao
	// criadas junto com o frame, mesmo as que ainda nao aparecem.
	g_pose = FindPosePanel(frame);
	if (!g_pose.ok)
		return false;

	if (!RunOnUiThread(frame, SubclassHere, nullptr))
		return false;

	g_installed = true;
	LogF("mirror pose: ligado no painel %p", static_cast<void*>(g_pose.panel));
	return true;
}

void Uninstall() {
	if (g_installed && g_frame)
		RunOnUiThread(g_frame, UnsubclassHere, nullptr);
	g_pose = PosePanel();
	g_frame = nullptr;
	g_installed = false;
	g_applying = false;
	g_lastUnmirrorable.clear();
}

} // namespace MirrorPose

BSOS_REGISTER_FEATURE(mirrorpose, "mirror de pose", HostApp::OutfitStudio, Enabled,
					  MirrorPose::Install, MirrorPose::Uninstall)
