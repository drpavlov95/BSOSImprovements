// Proxy msimg32.dll -- repassa os cinco exports para o msimg32 real do System32.
//
// Wrappers tipados, nao thunks em assembly: MSVC x64 nao aceita __declspec(naked).
// O nome msimg32 foi escolhido porque os dois exes do BodySlide o importam
// estaticamente, ele nao e KnownDLL, e tem a menor superficie de export entre os
// candidatos. version.dll nao serve: pertence ao mod de Drape do usuario.

#include <windows.h>

#include "core/host.h"

namespace {

HMODULE g_real = nullptr;

// Resolve uma funcao do msimg32 real. Carrega sob demanda pelo caminho absoluto
// do System32 -- carregar por nome pegaria este mesmo DLL de volta.
FARPROC Real(const char* name) {
	if (!g_real) {
		wchar_t path[MAX_PATH];
		UINT n = GetSystemDirectoryW(path, MAX_PATH);
		if (n == 0 || n >= MAX_PATH - 16)
			return nullptr;
		lstrcatW(path, L"\\msimg32.dll");
		g_real = LoadLibraryW(path);
	}
	return g_real ? GetProcAddress(g_real, name) : nullptr;
}

} // namespace

// As implementacoes usam nomes proprios e o .def as reexporta com os nomes
// oficiais. Definir AlphaBlend/GradientFill/TransparentBlt diretamente colidiria
// com as declaracoes dllimport de wingdi.h (C4273).
extern "C" {

BOOL WINAPI Proxy_AlphaBlend(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
							 HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc,
							 BLENDFUNCTION bf) {
	using Fn = BOOL(WINAPI*)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
	static Fn fn = reinterpret_cast<Fn>(Real("AlphaBlend"));
	return fn ? fn(hdcDest, xDest, yDest, wDest, hDest, hdcSrc, xSrc, ySrc, wSrc, hSrc, bf) : FALSE;
}

BOOL WINAPI Proxy_GradientFill(HDC hdc, PTRIVERTEX vertex, ULONG nVertex,
							   PVOID mesh, ULONG nMesh, ULONG mode) {
	using Fn = BOOL(WINAPI*)(HDC, PTRIVERTEX, ULONG, PVOID, ULONG, ULONG);
	static Fn fn = reinterpret_cast<Fn>(Real("GradientFill"));
	return fn ? fn(hdc, vertex, nVertex, mesh, nMesh, mode) : FALSE;
}

BOOL WINAPI Proxy_TransparentBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
								 HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc,
								 UINT crTransparent) {
	using Fn = BOOL(WINAPI*)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
	static Fn fn = reinterpret_cast<Fn>(Real("TransparentBlt"));
	return fn ? fn(hdcDest, xDest, yDest, wDest, hDest, hdcSrc, xSrc, ySrc, wSrc, hSrc, crTransparent) : FALSE;
}

// Os dois abaixo sao legados e nao sao chamados por aplicacoes normais.
// Existem so para o conjunto de exports ficar completo.

HRESULT WINAPI Proxy_DllInitialize(PVOID a, DWORD b, PVOID c) {
	using Fn = HRESULT(WINAPI*)(PVOID, DWORD, PVOID);
	static Fn fn = reinterpret_cast<Fn>(Real("DllInitialize"));
	return fn ? fn(a, b, c) : E_NOTIMPL;
}

void WINAPI Proxy_vSetDdrawflag(void) {
	using Fn = void(WINAPI*)(void);
	static Fn fn = reinterpret_cast<Fn>(Real("vSetDdrawflag"));
	if (fn)
		fn();
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);
		// So cria a thread. Todo o trabalho real acontece fora do loader lock,
		// depois que a UI da aplicacao existir.
		HostStartup(hModule);
	}
	else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
		// reserved == nullptr significa FreeLibrary explicito. No encerramento
		// do processo (reserved != nullptr) nao se mexe em mais nada.
		HostShutdown();
	}
	return TRUE;
}
