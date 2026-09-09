#include "xrcmap.h"

#include "core/log.h"

namespace {

std::string ReadWholeFile(const wchar_t* path) {
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
						   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return std::string();

	LARGE_INTEGER size = {};
	if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (32 << 20)) {
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

// Le o valor de um atributo. Aceita aspas simples e duplas.
std::string Attribute(const std::string& tag, const char* key) {
	std::string needle = std::string(key) + "=";
	size_t at = tag.find(needle);
	if (at == std::string::npos)
		return std::string();

	size_t quotePos = at + needle.size();
	if (quotePos >= tag.size())
		return std::string();

	char quote = tag[quotePos];
	if (quote != '"' && quote != '\'')
		return std::string();

	size_t begin = quotePos + 1;
	size_t end = tag.find(quote, begin);
	if (end == std::string::npos)
		return std::string();

	return tag.substr(begin, end - begin);
}

struct Token {
	enum Kind { Open, Close, SelfClose, End };
	Kind kind = End;
	std::string name;
	std::string cls;
	std::string label;   // <label> do proprio objeto, ainda cru
	std::string tooltip; // <tooltip>, idem
};

std::string DecodeEntities(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size();) {
		if (s[i] == '&') {
			if (s.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 5; continue; }
			if (s.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 4; continue; }
			if (s.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 4; continue; }
			if (s.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 6; continue; }
			if (s.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 6; continue; }
		}
		out.push_back(s[i]);
		++i;
	}
	return out;
}

// Deixa o rotulo do XRC no mesmo formato do rotulo lido do menu vivo.
std::wstring NormalizeXrcLabel(const std::string& raw) {
	std::string text = DecodeEntities(raw);

	// O acelerador nao faz parte do nome. No XRC ele vem como "\t" literal --
	// dois caracteres, barra e t -- e nao como um tab de verdade.
	size_t cut = text.find("\\t");
	if (cut != std::string::npos)
		text.resize(cut);
	cut = text.find('\t');
	if (cut != std::string::npos)
		text.resize(cut);

	std::string clean;
	for (char c : text) {
		if (c != '&') // mnemonico
			clean.push_back(c);
	}

	size_t begin = clean.find_first_not_of(" \r\n");
	if (begin == std::string::npos)
		return std::wstring();
	size_t end = clean.find_last_not_of(" \r\n");
	clean = clean.substr(begin, end - begin + 1);

	const int wide = MultiByteToWideChar(CP_UTF8, 0, clean.c_str(),
										 static_cast<int>(clean.size()), nullptr, 0);
	if (wide <= 0)
		return std::wstring();

	std::wstring out(static_cast<size_t>(wide), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, clean.c_str(), static_cast<int>(clean.size()),
						out.data(), wide);
	return out;
}

// Varredor minimo de tags. So enxerga <object>, </object> e <object/>;
// qualquer outra tag (<label>, <help>, <enabled>) e ignorada, o que e
// exatamente o que precisamos: dentro de um menu, so <object> conta posicao.
class Scanner {
public:
	explicit Scanner(const std::string& text) : text_(text) {}

	Token Next() {
		for (;;) {
			size_t lt = text_.find('<', pos_);
			if (lt == std::string::npos)
				return Token();

			// Comentarios podem conter '<' e '>' soltos.
			if (text_.compare(lt, 4, "<!--") == 0) {
				size_t close = text_.find("-->", lt);
				pos_ = (close == std::string::npos) ? text_.size() : close + 3;
				continue;
			}

			size_t gt = text_.find('>', lt);
			if (gt == std::string::npos)
				return Token();

			std::string tag = text_.substr(lt + 1, gt - lt - 1);
			pos_ = gt + 1;

			if (tag.compare(0, 7, "/object") == 0) {
				Token t;
				t.kind = Token::Close;
				return t;
			}
			if (tag.compare(0, 6, "object") != 0)
				continue; // outra tag qualquer

			Token t;
			t.kind = (!tag.empty() && tag.back() == '/') ? Token::SelfClose : Token::Open;
			t.cls = Attribute(tag, "class");
			t.name = Attribute(tag, "name");
			t.label = PeekTag("<label>", "</label>");
			t.tooltip = PeekTag("<tooltip>", "</tooltip>");
			return t;
		}
	}

private:
	// Uma tag simples DESTE objeto, sem consumir nada. So conta se vier antes
	// de qualquer <object> filho e antes do </object> que fecha este -- senao
	// seria a tag de outro item.
	std::string PeekTag(const char* openTag, const char* closeTag) const {
		const size_t open = text_.find(openTag, pos_);
		if (open == std::string::npos)
			return std::string();

		const size_t child = text_.find("<object", pos_);
		if (child != std::string::npos && child < open)
			return std::string();

		const size_t close = text_.find("</object>", pos_);
		if (close != std::string::npos && close < open)
			return std::string();

		const size_t end = text_.find(closeTag, open);
		if (end == std::string::npos)
			return std::string();

		const size_t begin = open + strlen(openTag);
		return text_.substr(begin, end - begin);
	}

	const std::string& text_;
	size_t pos_ = 0;
};

// Percorre os filhos <object> do container atual, contando posicao.
// Consome ate o </object> correspondente.
bool WalkChildren(Scanner& scanner, const std::string& target, MenuTrail& trail) {
	int index = 0;
	for (;;) {
		Token tok = scanner.Next();
		if (tok.kind == Token::End || tok.kind == Token::Close)
			return false;

		trail.path.push_back(index);
		trail.labels.push_back(NormalizeXrcLabel(tok.label));
		if (tok.name == target)
			return true;

		// SelfClose nao tem conteudo; so Open abre um nivel.
		if (tok.kind == Token::Open && WalkChildren(scanner, target, trail))
			return true;

		trail.path.pop_back();
		trail.labels.pop_back();
		++index;
	}
}

} // namespace

namespace {

std::wstring WidenUtf8(const std::string& text) {
	if (text.empty())
		return std::wstring();

	const int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
										 static_cast<int>(text.size()), nullptr, 0);
	if (wide <= 0)
		return std::wstring();

	std::wstring out(static_cast<size_t>(wide), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
						out.data(), wide);
	return out;
}

std::wstring TrimWide(const std::wstring& s) {
	const size_t begin = s.find_first_not_of(L" \t\r\n");
	if (begin == std::wstring::npos)
		return std::wstring();
	const size_t end = s.find_last_not_of(L" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

// O texto de um tooltip do XRC, pronto para comparar com o que chega na tela.
//
// O "\n" do XRC e literal -- barra e ene, dois caracteres -- e o wx o converte
// em quebra de linha de verdade ao carregar. Sem essa conversao, o tooltip de
// duas linhas nunca casaria.
std::wstring NormalizeXrcTooltip(const std::string& raw) {
	const std::string decoded = DecodeEntities(raw);

	std::wstring out;
	const std::wstring wide = WidenUtf8(decoded);
	for (size_t i = 0; i < wide.size(); ++i) {
		if (wide[i] == L'\\' && i + 1 < wide.size() && wide[i + 1] == L'n') {
			out.push_back(L'\n');
			++i;
			continue;
		}
		out.push_back(wide[i]);
	}
	return TrimWide(out);
}

// O acelerador de um rotulo do XRC: o que vem depois do \t, que ali tambem e
// literal.
std::wstring AcceleratorFromXrcLabel(const std::string& raw) {
	const std::string decoded = DecodeEntities(raw);

	size_t cut = decoded.find("\\t");
	size_t skip = 2;
	if (cut == std::string::npos) {
		cut = decoded.find('\t');
		skip = 1;
	}
	if (cut == std::string::npos)
		return std::wstring();

	return TrimWide(WidenUtf8(decoded.substr(cut + skip)));
}

} // namespace

XrcShortcuts ResolveXrcShortcuts(const wchar_t* xrcFile) {
	XrcShortcuts out;
	if (!xrcFile)
		return out;

	const std::string xml = ReadWholeFile(xrcFile);
	if (xml.empty()) {
		LogF("xrcmap: nao consegui ler o XRC para os atalhos de tooltip");
		return out;
	}

	Scanner scanner(xml);
	for (;;) {
		Token token = scanner.Next();
		if (token.kind == Token::End)
			break;
		if (token.name.empty())
			continue;

		if (token.cls == "tool") {
			const std::wstring tooltip = NormalizeXrcTooltip(token.tooltip);
			if (tooltip.empty())
				continue;

			// Dois tooltips iguais nao podem escolher sozinhos qual ferramenta
			// e qual. Melhor nenhum atalho que o atalho da outra.
			auto existing = out.toolByTooltip.find(tooltip);
			if (existing != out.toolByTooltip.end()) {
				existing->second.clear();
				continue;
			}
			out.toolByTooltip[tooltip] = token.name;
			continue;
		}

		if (token.cls == "wxMenuItem") {
			const std::wstring accel = AcceleratorFromXrcLabel(token.label);
			if (!accel.empty())
				out.acceleratorByName[token.name] = accel;
		}
	}

	return out;
}

MenuPath ResolveMenuPath(const wchar_t* xrcFile, const char* xrcName) {
	return ResolveMenuTrail(xrcFile, xrcName).path;
}

MenuTrail ResolveMenuTrail(const wchar_t* xrcFile, const char* xrcName) {
	MenuTrail trail;
	if (!xrcFile || !xrcName || !*xrcName)
		return trail;

	std::string xml = ReadWholeFile(xrcFile);
	if (xml.empty()) {
		LogF("xrcmap: nao consegui ler o XRC");
		return trail;
	}

	// Avanca ate o menubar. Objetos anteriores (frame, toolbars) nao contam --
	// a toolbar do Outfit Studio inclusive repete names de itens de menu.
	Scanner scanner(xml);
	for (;;) {
		Token tok = scanner.Next();
		if (tok.kind == Token::End) {
			LogF("xrcmap: nenhum wxMenuBar no XRC");
			return trail;
		}
		if (tok.kind == Token::Open && tok.cls == "wxMenuBar")
			break;
	}

	if (!WalkChildren(scanner, xrcName, trail)) {
		trail.path.clear();
		trail.labels.clear();
	}

	return trail;
}
