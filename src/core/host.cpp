#include "core/host.h"

#include <cwctype>

#include "core/log.h"
#include "features/registry.h"

namespace {

HMODULE g_self = nullptr;
HANDLE g_thread = nullptr;
HostApp g_app = HostApp::Unknown;
Config g_config;
HWND g_frame = nullptr;
std::wstring g_appDir;
bool g_installed = false;

std::wstring Lower(std::wstring s) {
	for (wchar_t& c : s)
		c = static_cast<wchar_t>(std::towlower(c));
	return s;
}

std::wstring BaseName(const std::wstring& path) {
	size_t slash = path.find_last_of(L"\\/");
	return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

struct FrameSearch {
	DWORD pid = 0;
	HWND withMenu = nullptr;
	HWND anyTitled = nullptr;
};

BOOL CALLBACK FrameEnumProc(HWND hwnd, LPARAM param) {
	auto* search = reinterpret_cast<FrameSearch*>(param);

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid != search->pid)
		return TRUE;
	if (!IsWindowVisible(hwnd))
		return TRUE;
	// Dialogos e popups tem owner; o frame principal nao.
	if (GetWindow(hwnd, GW_OWNER) != nullptr)
		return TRUE;
	if (GetWindowTextLengthW(hwnd) == 0)
		return TRUE;

	// O frame do Outfit Studio tem menubar; o do BodySlide nao tem nenhuma
	// (BodySlide.xrc nao declara wxMenuBar). Por isso o menu e preferencia,
	// nao requisito.
	if (GetMenu(hwnd) != nullptr) {
		search->withMenu = hwnd;
		return FALSE;
	}
	if (!search->anyTitled)
		search->anyTitled = hwnd;
	return TRUE;
}

HWND FindMainFrame() {
	FrameSearch search;
	search.pid = GetCurrentProcessId();
	EnumWindows(FrameEnumProc, reinterpret_cast<LPARAM>(&search));
	return search.withMenu ? search.withMenu : search.anyTitled;
}

void InstallFeatures() {
	for (const FeatureDef& feature : RegisteredFeatures()) {
		if (feature.app != g_app)
			continue;
		if (!feature.enabled(g_config)) {
			LogF("feature '%s' desligada no INI", feature.name);
			continue;
		}
		bool ok = feature.install(g_frame);
		LogF("feature '%s': %s", feature.name, ok ? "instalada" : "FALHOU ao instalar");
	}
	g_installed = true;
}

DWORD WINAPI BootstrapThread(LPVOID) {
	wchar_t exePath[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		return 0;

	g_app = DetectApp(exePath);
	if (g_app == HostApp::Unknown)
		return 0; // outro processo qualquer que importe msimg32

	std::wstring full(exePath);
	size_t slash = full.find_last_of(L"\\/");
	g_appDir = (slash == std::wstring::npos) ? std::wstring() : full.substr(0, slash + 1);

	const bool isBodySlide = (g_app == HostApp::BodySlide);

	g_config = LoadConfig((g_appDir + L"BSOSImprovements.ini").c_str());
	LogInit(g_config.logFile, isBodySlide ? L"BodySlide" : L"OutfitStudio");
	LogF("BSOSImprovements: %s detectado em %ls", isBodySlide ? "BodySlide" : "OutfitStudio", exePath);

	// A UI sobe bem depois do loader. Espera ate 30s, sem travar nada.
	for (int attempt = 0; attempt < 300; ++attempt) {
		g_frame = FindMainFrame();
		if (g_frame)
			break;
		Sleep(100);
	}

	if (!g_frame) {
		LogF("nenhuma janela principal apareceu em 30s -- desistindo");
		return 0;
	}

	LogF("janela principal: %p", static_cast<void*>(g_frame));
	InstallFeatures();
	return 0;
}

} // namespace

HostApp DetectApp(const wchar_t* exePath) {
	if (!exePath || !*exePath)
		return HostApp::Unknown;

	std::wstring name = Lower(BaseName(exePath));
	// Testa OutfitStudio primeiro: nomes distintos hoje, mas a ordem torna a
	// intencao explicita se algum dia um contiver o outro.
	if (name.find(L"outfitstudio") != std::wstring::npos)
		return HostApp::OutfitStudio;
	if (name.find(L"bodyslide") != std::wstring::npos)
		return HostApp::BodySlide;
	return HostApp::Unknown;
}

void HostStartup(HMODULE self) {
	g_self = self;
	g_thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
}

void HostShutdown() {
	if (!g_installed)
		return;
	for (const FeatureDef& feature : RegisteredFeatures()) {
		if (feature.app == g_app)
			feature.uninstall();
	}
	g_installed = false;
}

HostApp CurrentApp() {
	return g_app;
}

const Config& Cfg() {
	return g_config;
}

HWND MainFrame() {
	if (g_frame && !IsWindow(g_frame))
		g_frame = FindMainFrame();
	return g_frame;
}

const std::wstring& AppDir() {
	return g_appDir;
}
