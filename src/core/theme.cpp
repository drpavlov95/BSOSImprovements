#include "core/theme.h"

#include <uxtheme.h>

#include <cctype>
#include <vector>

#include "core/log.h"

namespace {

std::string Trim(const std::string& s) {
	size_t begin = s.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return std::string();
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

bool EqualsNoCase(const std::string& a, const char* b) {
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) !=
			std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return i == a.size() && b[i] == '\0';
}

std::string ReadWholeFile(const std::wstring& path) {
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
						   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return std::string();

	LARGE_INTEGER size = {};
	if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (8 << 20)) {
		CloseHandle(h);
		return std::string();
	}

	std::string text(static_cast<size_t>(size.QuadPart), '\0');
	DWORD read = 0;
	BOOL ok = ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
	CloseHandle(h);
	if (!ok)
		return std::string();

	text.resize(read);
	return text;
}

} // namespace

Appearance ResolveAppearance(const std::string& mode, bool systemPrefersDark) {
	const std::string value = Trim(mode);
	if (EqualsNoCase(value, "Dark"))
		return Appearance::Dark;
	if (EqualsNoCase(value, "System"))
		return systemPrefersDark ? Appearance::Dark : Appearance::Light;
	return Appearance::Light;
}

bool SystemPrefersDark() {
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
					  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
					  0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
		return false;

	DWORD value = 1; // ausente = claro
	DWORD size = sizeof(value);
	DWORD type = 0;
	const LSTATUS status =
		RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
						 reinterpret_cast<BYTE*>(&value), &size);
	RegCloseKey(key);

	if (status != ERROR_SUCCESS || type != REG_DWORD)
		return false;
	return value == 0;
}

Appearance DetectAppearance(const std::wstring& appDir) {
	const std::string xml = ReadWholeFile(appDir + L"Config.xml");
	if (xml.empty())
		return Appearance::Light;

	// Leitura crua da tag em vez de parser XML: e um unico elemento simples, e
	// nao vale arrastar um parser para dentro do mod por causa dele.
	const std::string open = "<AppearanceMode>";
	const size_t at = xml.find(open);
	if (at == std::string::npos)
		return Appearance::Light; // ausente: o default do BodySlide e claro

	const size_t begin = at + open.size();
	const size_t end = xml.find("</AppearanceMode>", begin);
	if (end == std::string::npos)
		return Appearance::Light;

	return ResolveAppearance(xml.substr(begin, end - begin), SystemPrefersDark());
}

HBRUSH DarkBackgroundBrush() {
	static HBRUSH brush = CreateSolidBrush(kDarkBackground);
	return brush;
}

HBRUSH EditBackgroundBrush() {
	static HBRUSH brush = CreateSolidBrush(kDarkControlBackground);
	return brush;
}

void ApplyDarkControlTheme(HWND control, bool isEdit) {
	if (!control)
		return;
	// O wx ja pos o processo em modo escuro; isto so pede a cada controle a
	// variante escura do tema. "DarkMode_CFD" e a das caixas de texto, com a
	// borda certa; "DarkMode_Explorer" serve para lista, botao e barra.
	SetWindowTheme(control, isEdit ? L"DarkMode_CFD" : L"DarkMode_Explorer", nullptr);
}
