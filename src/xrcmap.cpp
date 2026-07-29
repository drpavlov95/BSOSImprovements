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
};

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
			return t;
		}
	}

private:
	const std::string& text_;
	size_t pos_ = 0;
};

// Percorre os filhos <object> do container atual, contando posicao.
// Consome ate o </object> correspondente.
bool WalkChildren(Scanner& scanner, const std::string& target, MenuPath& path) {
	int index = 0;
	for (;;) {
		Token tok = scanner.Next();
		if (tok.kind == Token::End || tok.kind == Token::Close)
			return false;

		path.push_back(index);
		if (tok.name == target)
			return true;

		// SelfClose nao tem conteudo; so Open abre um nivel.
		if (tok.kind == Token::Open && WalkChildren(scanner, target, path))
			return true;

		path.pop_back();
		++index;
	}
}

} // namespace

MenuPath ResolveMenuPath(const wchar_t* xrcFile, const char* xrcName) {
	MenuPath path;
	if (!xrcFile || !xrcName || !*xrcName)
		return path;

	std::string xml = ReadWholeFile(xrcFile);
	if (xml.empty()) {
		LogF("xrcmap: nao consegui ler o XRC");
		return path;
	}

	// Avanca ate o menubar. Objetos anteriores (frame, toolbars) nao contam --
	// a toolbar do Outfit Studio inclusive repete names de itens de menu.
	Scanner scanner(xml);
	for (;;) {
		Token tok = scanner.Next();
		if (tok.kind == Token::End) {
			LogF("xrcmap: nenhum wxMenuBar no XRC");
			return path;
		}
		if (tok.kind == Token::Open && tok.cls == "wxMenuBar")
			break;
	}

	if (!WalkChildren(scanner, xrcName, path))
		path.clear();

	return path;
}
