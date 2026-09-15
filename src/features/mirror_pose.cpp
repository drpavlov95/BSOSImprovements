#include "features/mirror_pose.h"

#include <commctrl.h>

#include <vector>

#include "core/host.h"
#include "core/log.h"
#include "core/theme.h"
#include "core/ui_thread.h"
#include "features/pose_panel.h"
#include "features/registry.h"

namespace {

const int kPoseRows = 7;
const int kMirrorPoseCheckId = 0xB50E;
const UINT_PTR kSubclassId = 0xB50D;

PosePanel g_pose;
HWND g_frame = nullptr;
bool g_installed = false;
HWND g_mirrorCheck = nullptr;

// Enquanto escrevemos no painel, as nossas proprias mensagens voltam pela
// subclasse. Sem esta trava o espelho dispararia o espelho, e o segundo
// espelharia de volta para o primeiro osso -- um vai e volta infinito com a
// interface travada no meio.
bool g_applying = false;

// Um arrasto normalmente termina com duas mensagens equivalentes
// (TB_THUMBPOSITION e TB_ENDTRACK). Guardamos a última pose aplicada para que
// a segunda mensagem não troque a seleção nem escreva os mesmos sete valores
// outra vez.
bool g_haveLastApplied = false;
std::wstring g_lastAppliedBone;
int g_lastAppliedValues[kPoseRows] = {};

// Para nao repetir o mesmo aviso a cada arrasto num osso central.
std::wstring g_lastUnmirrorable;

// O wx organiza esta linha como "Show Pose | lista de ossos | Reset Bone".
// Reservamos um pequeno trecho da lista para a caixa nova, mantendo o botao
// Reset no lugar e sem encobrir nenhum controle do programa.
void LayoutMirrorCheck() {
	if (!g_mirrorCheck || !g_pose.showPose || !g_pose.boneChoice)
		return;

	RECT show = {};
	RECT bone = {};
	if (!GetWindowRect(g_pose.showPose, &show) || !GetWindowRect(g_pose.boneChoice, &bone))
		return;
	ScreenToClient(g_pose.panel, reinterpret_cast<POINT*>(&show.left));
	ScreenToClient(g_pose.panel, reinterpret_cast<POINT*>(&show.right));
	ScreenToClient(g_pose.panel, reinterpret_cast<POINT*>(&bone.left));
	ScreenToClient(g_pose.panel, reinterpret_cast<POINT*>(&bone.right));

	const int checkWidth = 92;
	const int checkX = show.right + 5;
	const int boneX = checkX + checkWidth + 5;
	const int boneRight = bone.right;
	if (boneRight - boneX < 60)
		return; // painel estreito demais: nao esmagar a lista de ossos.

	SetWindowPos(g_mirrorCheck, nullptr, checkX, show.top, checkWidth,
				 show.bottom - show.top, SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(g_pose.boneChoice, nullptr, boneX, bone.top, boneRight - boneX,
				 bone.bottom - bone.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool MirrorCheckEnabled() {
	return g_mirrorCheck && SendMessageW(g_mirrorCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

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

// Alguns rigs usam apelidos como [Ltgh] e [Luar], com a letra seguinte
// minuscula. Esta variante so e usada se o resultado existir literalmente na
// lista de ossos, portanto nao inventa um par para um osso central como Root.
bool FlipTokenForExistingPair(std::wstring& token) {
	if (FlipToken(token))
		return true;
	if (token.size() > 2 && token[0] == L'[') {
		const wchar_t flipped = FlipSide(token[1]);
		if (flipped) {
			token[1] = flipped;
			return true;
		}
	}
	if (token.size() > 1) {
		const wchar_t flipped = FlipSide(token[0]);
		if (flipped) {
			token[0] = flipped;
			return true;
		}
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

std::wstring MirrorBoneNameForExistingPair(const std::wstring& name) {
	std::vector<std::wstring> tokens = SplitOnSpaces(name);
	bool changed = false;
	for (std::wstring& token : tokens)
		changed = FlipTokenForExistingPair(token) || changed;
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

int FindMirrorTarget(HWND combo, int current, const std::wstring& bone, std::wstring& outName) {
	for (const std::wstring& candidate : {MirrorBoneName(bone), MirrorBoneNameForExistingPair(bone)}) {
		if (candidate.empty())
			continue;
		const int found = static_cast<int>(SendMessageW(combo, CB_FINDSTRINGEXACT,
				static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(candidate.c_str())));
		if (found >= 0 && found != current) {
			outName = candidate;
			return found;
		}
	}
	return -1;
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

// Selecionar outro bone faz o Outfit Studio repintar os controles de pose,
// mas esse repaint pode ficar enfileirado quando a seleção acontece dentro do
// mesmo ciclo de mensagens do arrasto. Depois de voltar ao bone original,
// restauramos os sete handles e os sete textos capturados antes da operação.
// Isso é apenas uma sincronização visual: os valores internos do bone original
// nunca são escritos de volta nem passam por um segundo evento de pose.
void RestorePoseControls(const int values[kPoseRows],
						 const std::wstring texts[kPoseRows]) {
	for (int i = 0; i < kPoseRows; ++i) {
		if (g_pose.sliders[i])
			SendMessageW(g_pose.sliders[i], TBM_SETPOS, TRUE,
						  static_cast<LPARAM>(values[i]));
		if (g_pose.texts[i])
			SetWindowTextW(g_pose.texts[i], texts[i].c_str());
	}
}

void SetPoseControlsRedraw(bool redraw) {
	std::vector<HWND> controls;
	controls.reserve(2 + kPoseRows * 2);
	controls.push_back(g_pose.panel);
	controls.push_back(g_pose.boneChoice);
	for (int i = 0; i < kPoseRows; ++i) {
		controls.push_back(g_pose.sliders[i]);
		controls.push_back(g_pose.texts[i]);
	}
	for (HWND control : controls)
		if (control)
			SendMessageW(control, WM_SETREDRAW, redraw ? TRUE : FALSE, 0);

	if (!redraw)
		return;

	// Nunca peça apagamento de fundo aqui. Durante a troca temporária de bone,
	// o wxMSW pinta o painel com branco antes de repintar os filhos; esse erase
	// é o clarão branco visível por uma fração de segundo. RDW_NOERASE conserva
	// o fundo já pintado e ainda força os handles/textos a aparecerem juntos.
	for (HWND control : controls)
		if (control)
			RedrawWindow(control, nullptr, nullptr,
						 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
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
	if (g_applying || !g_pose.ok || !MirrorCheckEnabled())
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
	std::wstring mirrored;
	const int target = FindMirrorTarget(g_pose.boneChoice, current, bone, mirrored);
	if (target < 0) {
		if (g_lastUnmirrorable != bone) {
			g_lastUnmirrorable = bone;
			LogF("mirror pose: '%ls' nao tem um par L/R correspondente na lista", bone.c_str());
		}
		return;
	}

	int values[kPoseRows] = {};
	std::wstring textValues[kPoseRows];
	for (int i = 0; i < kPoseRows; ++i)
		values[i] = static_cast<int>(SendMessageW(ordered[i], TBM_GETPOS, 0, 0));
	if (g_haveLastApplied && g_lastAppliedBone == bone) {
		bool same = true;
		for (int i = 0; i < kPoseRows; ++i) {
			if (values[i] != g_lastAppliedValues[i]) {
				same = false;
				break;
			}
		}
		if (same)
			return;
	}
	for (int i = 0; i < kPoseRows; ++i) {
		const int length = GetWindowTextLengthW(g_pose.texts[i]);
		if (length > 0 && length < 128) {
			textValues[i].resize(static_cast<size_t>(length) + 1);
			GetWindowTextW(g_pose.texts[i], textValues[i].data(), length + 1);
			textValues[i].resize(static_cast<size_t>(length));
		}
	}

	int flipped[kPoseRows] = {};
	MirrorValues(values, flipped, Cfg().mirrorSigns);

	g_applying = true;
	// Trocar a lista e atualizar as barras e necessario para usar o caminho
	// nativo do Outfit Studio, mas nao deve ser visivel como um "pulo".
	SetPoseControlsRedraw(false);
	SelectBone(target);
	for (int i = 0; i < kPoseRows; ++i)
		WriteSlider(ordered[i], flipped[i]);
	SelectBone(current);
	RestorePoseControls(values, textValues);
	SetPoseControlsRedraw(true);
	g_applying = false;
	g_haveLastApplied = true;
	g_lastAppliedBone = bone;
	for (int i = 0; i < kPoseRows; ++i)
		g_lastAppliedValues[i] = values[i];

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
		g_mirrorCheck = nullptr;
		g_pose = PosePanel();
	}

	// Deixa o Outfit Studio tratar primeiro: o espelho tem que partir do valor
	// que ele ja aplicou, nao do que estava antes da mensagem.
	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

	if (g_applying)
		return result;

	if (msg == WM_SIZE)
		LayoutMirrorCheck();

	// TB_ENDTRACK cobre a soltura e TB_THUMBPOSITION cobre o clique na régua.
	// O espelhamento contínuo em TB_THUMBTRACK não é seguro neste host: trocar
	// a seleção enquanto o wx ainda mantém o mouse capturado causa reentrada,
	// atraso e pode terminar em exceção fatal. g_applying impede que as mensagens
	// sintéticas do alvo entrem aqui.
	if (msg == WM_HSCROLL &&
		(LOWORD(wParam) == TB_ENDTRACK || LOWORD(wParam) == TB_THUMBPOSITION) &&
		IsPoseSlider(reinterpret_cast<HWND>(lParam))) {
		ApplyMirror();
	}
	// Quem digita o valor no campo em vez de arrastar sai dele quando termina.
	else if (msg == WM_COMMAND && HIWORD(wParam) == EN_KILLFOCUS &&
				 IsPoseText(reinterpret_cast<HWND>(lParam))) {
		ApplyMirror();
	}
	// Marcar Mirror pose deve aplicar o estado que já existe imediatamente;
	// esperar outro arrasto deixava a caixa aparentemente sem efeito.
	else if (msg == WM_COMMAND && LOWORD(wParam) == kMirrorPoseCheckId &&
				 HIWORD(wParam) == BN_CLICKED) {
		if (MirrorCheckEnabled())
			ApplyMirror();
		else
			g_haveLastApplied = false;
	}

	return result;
}

void SubclassHere(void*) {
	if (!SetWindowSubclass(g_pose.panel, PanelSubclassProc, kSubclassId, 0))
		LogF("mirror pose: nao consegui subclassar o painel");

	// O controle e filho do painel verdadeiro, portanto acompanha o recolher/
	// expandir do Posing e usa a mesma fonte da caixa Show Pose.
	g_mirrorCheck = CreateWindowExW(0, L"BUTTON", L"Mirror pose",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
			0, 0, 0, 0, g_pose.panel,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMirrorPoseCheckId)), SelfModule(), nullptr);
	if (!g_mirrorCheck) {
		LogF("mirror pose: nao consegui criar a caixa de ativacao");
		return;
	}
	SendMessageW(g_mirrorCheck, WM_SETFONT,
				 static_cast<WPARAM>(SendMessageW(g_pose.showPose, WM_GETFONT, 0, 0)), TRUE);
	// SetWindowTheme("DarkMode_Explorer") pinta o texto de branco mesmo dentro
	// de um Outfit Studio claro. So apele explicitamente para a variante escura
	// quando o proprio Config.xml realmente esta naquele modo; no claro, o
	// checkbox padrao herda as mesmas cores de Show Pose.
	if (DetectAppearance(AppDir()) == Appearance::Dark)
		ApplyDarkControlTheme(g_mirrorCheck, false);
	LayoutMirrorCheck();
}

void UnsubclassHere(void*) {
	if (g_mirrorCheck && IsWindow(g_mirrorCheck))
		DestroyWindow(g_mirrorCheck);
	g_mirrorCheck = nullptr;
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
	g_haveLastApplied = false;
	g_lastAppliedBone.clear();
	g_lastUnmirrorable.clear();
}

} // namespace MirrorPose

BSOS_REGISTER_FEATURE(mirrorpose, "mirror de pose", HostApp::OutfitStudio, Enabled,
					  MirrorPose::Install, MirrorPose::Uninstall)
