#include "core/osp.h"

#include <algorithm>
#include <map>

namespace {

// Um bloco <Slider>...</Slider> no texto, com o nome dele.
//
// O intervalo comeca no INICIO DA LINHA da tag de abertura e termina depois da
// quebra que fecha o bloco. Pegar a linha inteira e o que faz a indentacao
// viajar junto com o bloco: sem isso, reordenar embaralharia os espacos e o
// arquivo sairia torto.
struct SliderBlock {
	std::string name;
	size_t begin = 0;
	size_t end = 0;
};

size_t LineStart(const std::string& text, size_t pos) {
	const size_t newline = text.find_last_of('\n', pos);
	return (newline == std::string::npos) ? 0 : newline + 1;
}

size_t LineEnd(const std::string& text, size_t pos) {
	const size_t newline = text.find('\n', pos);
	return (newline == std::string::npos) ? text.size() : newline + 1;
}

std::string AttributeValue(const std::string& text, size_t tagBegin, size_t tagEnd,
						   const std::string& key) {
	const std::string needle = key + "=\"";
	const size_t at = text.find(needle, tagBegin);
	if (at == std::string::npos || at >= tagEnd)
		return std::string();

	const size_t valueBegin = at + needle.size();
	const size_t valueEnd = text.find('"', valueBegin);
	if (valueEnd == std::string::npos || valueEnd > tagEnd)
		return std::string();

	return text.substr(valueBegin, valueEnd - valueBegin);
}

// "<Slider" seguido de espaco, barra ou fecha-tag.
//
// A checagem do caractere seguinte NAO e detalhe: "<SliderSet" tambem comeca com
// "<Slider", e sem ela o conjunto inteiro seria confundido com um slider.
bool IsSliderTagAt(const std::string& text, size_t pos) {
	static const std::string kTag = "<Slider";
	if (text.compare(pos, kTag.size(), kTag) != 0)
		return false;

	const size_t after = pos + kTag.size();
	if (after >= text.size())
		return false;

	const char c = text[after];
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '>';
}

// O intervalo do conjunto pedido: do "<SliderSet" ate depois do "</SliderSet>".
bool FindSetRange(const std::string& xml, const std::string& setName, size_t& begin, size_t& end) {
	size_t search = 0;
	for (;;) {
		const size_t open = xml.find("<SliderSet", search);
		if (open == std::string::npos)
			return false;

		const size_t tagEnd = xml.find('>', open);
		if (tagEnd == std::string::npos)
			return false;

		if (AttributeValue(xml, open, tagEnd, "name") == setName) {
			const size_t close = xml.find("</SliderSet>", tagEnd);
			if (close == std::string::npos)
				return false;
			begin = open;
			end = close;
			return true;
		}
		search = tagEnd + 1;
	}
}

std::vector<std::string> SliderSetNames(const std::string& xml) {
	std::vector<std::string> names;
	size_t search = 0;
	for (;;) {
		const size_t open = xml.find("<SliderSet", search);
		if (open == std::string::npos)
			break;
		const size_t after = open + 10;
		if (after < xml.size() && xml[after] != ' ' && xml[after] != '\t' &&
			xml[after] != '\r' && xml[after] != '\n' && xml[after] != '>') {
			search = after;
			continue; // <SliderSetInfo>
		}
		const size_t tagEnd = xml.find('>', open);
		if (tagEnd == std::string::npos)
			break;
		std::string name = AttributeValue(xml, open, tagEnd, "name");
		if (!name.empty())
			names.push_back(std::move(name));
		search = tagEnd + 1;
	}
	return names;
}

bool SameNames(const std::vector<std::string>& a, const std::vector<std::string>& b) {
	if (a.size() != b.size() || a.empty())
		return false;
	std::map<std::string, size_t> counts;
	for (const std::string& name : a)
		++counts[name];
	for (const std::string& name : b) {
		auto it = counts.find(name);
		if (it == counts.end() || it->second == 0)
			return false;
		--it->second;
	}
	return true;
}

// Os blocos de slider de um intervalo, na ordem do arquivo.
std::vector<SliderBlock> FindBlocks(const std::string& xml, size_t begin, size_t end) {
	std::vector<SliderBlock> blocks;

	size_t at = begin;
	while (at < end) {
		const size_t open = xml.find("<Slider", at);
		if (open == std::string::npos || open >= end)
			break;

		if (!IsSliderTagAt(xml, open)) {
			at = open + 1; // "<SliderSet" e coisa parecida
			continue;
		}

		const size_t tagEnd = xml.find('>', open);
		if (tagEnd == std::string::npos || tagEnd >= end)
			break;

		SliderBlock block;
		block.name = AttributeValue(xml, open, tagEnd, "name");
		block.begin = LineStart(xml, open);

		// Slider vazio fecha na propria tag: <Slider name="X" ... />
		if (xml[tagEnd - 1] == '/') {
			block.end = LineEnd(xml, tagEnd);
			at = tagEnd + 1;
		} else {
			const size_t close = xml.find("</Slider>", tagEnd);
			if (close == std::string::npos || close >= end)
				break;
			block.end = LineEnd(xml, close);
			at = close + 1;
		}

		if (block.name.empty())
			return std::vector<SliderBlock>(); // sem nome nao da para reordenar

		blocks.push_back(block);
	}

	return blocks;
}

} // namespace

std::vector<std::string> ReadOspSliderOrder(const std::string& xml, const std::string& setName) {
	std::vector<std::string> names;

	size_t begin = 0;
	size_t end = 0;
	if (!FindSetRange(xml, setName, begin, end))
		return names;

	for (const SliderBlock& block : FindBlocks(xml, begin, end))
		names.push_back(block.name);
	return names;
}

std::string ReorderOspSliders(const std::string& xml, const std::string& setName,
							  const std::vector<std::string>& order) {
	size_t setBegin = 0;
	size_t setEnd = 0;
	if (!FindSetRange(xml, setName, setBegin, setEnd))
		return std::string();

	const std::vector<SliderBlock> blocks = FindBlocks(xml, setBegin, setEnd);
	if (blocks.empty() || blocks.size() != order.size())
		return std::string();

	// Os blocos precisam estar em linhas proprias e sem se sobrepor. Num
	// arquivo numa linha so os intervalos se cruzariam, e trocar pedacos
	// cruzados de lugar destruiria o texto.
	for (size_t i = 0; i + 1 < blocks.size(); ++i) {
		if (blocks[i].end > blocks[i + 1].begin)
			return std::string();
	}

	// A ordem pedida tem que ser uma permutacao EXATA do que esta no arquivo.
	// Um nome a mais some do arquivo; um a menos duplica outro. Nos dois casos o
	// usuario perde trabalho sem ser avisado.
	std::vector<const SliderBlock*> reordered;
	reordered.reserve(order.size());
	std::vector<bool> used(blocks.size(), false);

	for (const std::string& name : order) {
		const SliderBlock* found = nullptr;
		for (size_t i = 0; i < blocks.size(); ++i) {
			if (used[i] || blocks[i].name != name)
				continue;
			used[i] = true;
			found = &blocks[i];
			break;
		}
		if (!found)
			return std::string();
		reordered.push_back(found);
	}

	std::string out;
	out.reserve(xml.size());

	// Tudo antes do primeiro bloco sai intacto.
	out.append(xml, 0, blocks.front().begin);

	// Os blocos, na ordem nova, com o que houver entre eles preservado na
	// posicao original -- comentario ou linha em branco entre dois sliders
	// pertence ao lugar, nao ao bloco.
	for (size_t i = 0; i < reordered.size(); ++i) {
		out.append(xml, reordered[i]->begin, reordered[i]->end - reordered[i]->begin);
		if (i + 1 < blocks.size())
			out.append(xml, blocks[i].end, blocks[i + 1].begin - blocks[i].end);
	}

	// E tudo depois do ultimo.
	out.append(xml, blocks.back().end, xml.size() - blocks.back().end);
	return out;
}

std::string ReorderUniqueMatchingOspSliderSet(const std::string& xml,
										   const std::vector<std::string>& order,
										   std::string* matchedSet) {
	std::string match;
	for (const std::string& setName : SliderSetNames(xml)) {
		if (!SameNames(ReadOspSliderOrder(xml, setName), order))
			continue;
		if (!match.empty())
			return std::string(); // ambiguo: dois sets tem os mesmos sliders
		match = setName;
	}
	if (match.empty())
		return std::string();

	std::string out = ReorderOspSliders(xml, match, order);
	if (!out.empty() && matchedSet)
		*matchedSet = match;
	return out;
}
