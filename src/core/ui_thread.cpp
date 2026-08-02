#include "core/ui_thread.h"

#include "core/host.h"
#include "core/log.h"

namespace {

HHOOK g_trampoline = nullptr;
void (*g_fn)(void*) = nullptr;
void* g_ctx = nullptr;
volatile LONG g_ran = 0;

LRESULT CALLBACK Trampoline(int code, WPARAM wParam, LPARAM lParam) {
	// Roda so uma vez: o SendMessage que dispara isso pode ser precedido por
	// outras mensagens cruzando a mesma janela.
	if (code == HC_ACTION && InterlockedCompareExchange(&g_ran, 1, 0) == 0) {
		if (g_fn)
			g_fn(g_ctx);
	}
	return CallNextHookEx(g_trampoline, code, wParam, lParam);
}

} // namespace

bool RunOnUiThread(HWND target, void (*fn)(void*), void* ctx) {
	if (!target || !fn)
		return false;

	const DWORD uiThread = GetWindowThreadProcessId(target, nullptr);
	if (!uiThread)
		return false;

	if (uiThread == GetCurrentThreadId()) {
		fn(ctx);
		return true;
	}

	g_fn = fn;
	g_ctx = ctx;
	g_ran = 0;

	g_trampoline = SetWindowsHookExW(WH_CALLWNDPROC, Trampoline, SelfModule(), uiThread);
	if (!g_trampoline) {
		LogF("ui_thread: SetWindowsHookEx(WH_CALLWNDPROC) falhou (erro %lu)", GetLastError());
		return false;
	}

	// SendMessage entre threads bloqueia ate a thread de destino processar, o
	// que garante que o trampolim ja rodou quando isto retorna.
	//
	// Com timeout de proposito: um SendMessage sem limite trava a thread de
	// bootstrap para sempre se a UI estiver ocupada ou pendurada, e isso roda
	// na instalacao de todas as features. Melhor desistir e registrar do que
	// deixar uma thread parada dentro do processo do usuario.
	DWORD_PTR unused = 0;
	if (!SendMessageTimeoutW(target, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 5000, &unused))
		LogF("ui_thread: a janela nao respondeu em 5s (erro %lu)", GetLastError());

	UnhookWindowsHookEx(g_trampoline);
	g_trampoline = nullptr;
	g_fn = nullptr;
	g_ctx = nullptr;

	const bool ran = (g_ran != 0);
	if (!ran)
		LogF("ui_thread: o trampolim nao rodou");
	return ran;
}
