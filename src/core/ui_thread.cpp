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

	// SendMessage entre threads: bloqueia ate a thread de UI processar, o que
	// garante que o trampolim ja rodou quando isto retorna.
	SendMessageW(target, WM_NULL, 0, 0);

	UnhookWindowsHookEx(g_trampoline);
	g_trampoline = nullptr;
	g_fn = nullptr;
	g_ctx = nullptr;

	const bool ran = (g_ran != 0);
	if (!ran)
		LogF("ui_thread: o trampolim nao rodou");
	return ran;
}
