#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

bool g_enabled = false;
std::wstring g_path;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;

} // namespace

void LogInit(bool enabled, const wchar_t* tag) {
	g_enabled = enabled;
	if (!enabled)
		return;

	if (!g_lockReady) {
		InitializeCriticalSection(&g_lock);
		g_lockReady = true;
	}

	wchar_t dir[MAX_PATH] = {};
	if (!GetTempPathW(MAX_PATH, dir)) {
		g_enabled = false;
		return;
	}
	g_path = std::wstring(dir) + L"BSOSImprovements_" + (tag && *tag ? tag : L"Unknown") + L".log";

	// Trunca a cada sessao: um log de bug report deve conter so a sessao atual.
	HANDLE h = CreateFileW(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
						   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		g_enabled = false;
		return;
	}
	CloseHandle(h);
}

void LogF(const char* fmt, ...) {
	if (!g_enabled || !g_lockReady)
		return;

	char message[1024];
	va_list args;
	va_start(args, fmt);
	int written = _vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, args);
	va_end(args);
	if (written < 0)
		return;

	SYSTEMTIME t;
	GetLocalTime(&t);

	char line[1152];
	int total = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
							t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, message);
	if (total < 0)
		return;

	EnterCriticalSection(&g_lock);
	HANDLE h = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
						   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD ignored = 0;
		WriteFile(h, line, static_cast<DWORD>(total), &ignored, nullptr);
		CloseHandle(h);
	}
	LeaveCriticalSection(&g_lock);
}
