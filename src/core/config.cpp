#include "core/config.h"

#include <cctype>
#include <string>
#include <vector>

namespace {

std::string Trim(const std::string& s) {
	size_t begin = s.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return std::string();
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

std::string Lower(std::string s) {
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool ReadBool(const wchar_t* ini, const wchar_t* section, const wchar_t* key, bool fallback) {
	return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, ini) != 0;
}

// Le uma hotkey do INI. Chave ausente ou spec invalido devolvem o fallback --
// nunca uma hotkey invalida, senao a feature ficaria silenciosamente morta.
Hotkey ReadHotkey(const wchar_t* ini, const wchar_t* key, Hotkey fallback) {
	wchar_t wide[128] = {};
	GetPrivateProfileStringW(L"Hotkeys", key, L"", wide, 128, ini);
	if (wide[0] == L'\0')
		return fallback;

	std::string narrow;
	for (const wchar_t* p = wide; *p; ++p)
		narrow.push_back(*p < 128 ? static_cast<char>(*p) : '?');

	Hotkey parsed = ParseHotkey(narrow.c_str());
	return parsed.IsValid() ? parsed : fallback;
}

} // namespace

Hotkey ParseHotkey(const char* spec) {
	Hotkey invalid;
	if (!spec)
		return invalid;

	// Quebra em '+'. O ultimo token e a tecla; os anteriores sao modificadores.
	std::vector<std::string> parts;
	std::string s(spec);
	size_t start = 0;
	for (;;) {
		size_t plus = s.find('+', start);
		if (plus == std::string::npos) {
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, plus - start));
		start = plus + 1;
	}

	Hotkey hk;
	for (size_t i = 0; i + 1 < parts.size(); ++i) {
		std::string mod = Lower(Trim(parts[i]));
		if (mod == "shift")
			hk.shift = true;
		else if (mod == "ctrl" || mod == "control")
			hk.ctrl = true;
		else if (mod == "alt")
			hk.alt = true;
		else
			return invalid; // modificador desconhecido
	}

	std::string key = Trim(parts.back());
	if (key.size() != 1)
		return invalid;

	char c = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
	if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
		return invalid;

	hk.vk = static_cast<UINT>(static_cast<unsigned char>(c));
	return hk;
}

Config LoadConfig(const wchar_t* iniPath) {
	Config c;
	if (!iniPath || !*iniPath)
		return c;

	c.groupSearch = ReadBool(iniPath, L"Features", L"GroupSearch", c.groupSearch);
	c.referenceAutoSelect = ReadBool(iniPath, L"Features", L"ReferenceAutoSelect", c.referenceAutoSelect);
	c.sliderObjHotkeys = ReadBool(iniPath, L"Features", L"SliderOBJHotkeys", c.sliderObjHotkeys);
	c.referenceHotkey = ReadBool(iniPath, L"Features", L"ReferenceHotkey", c.referenceHotkey);

	c.selectReference = ReadHotkey(iniPath, L"SelectReference", c.selectReference);
	c.exportSliderObj = ReadHotkey(iniPath, L"ExportSliderOBJ", c.exportSliderObj);
	c.importSliderObj = ReadHotkey(iniPath, L"ImportSliderOBJ", c.importSliderObj);

	c.logFile = ReadBool(iniPath, L"Debug", L"LogFile", c.logFile);
	return c;
}
