#pragma once

#include <string>
#include <vector>

// Reordenar os sliders dentro de um arquivo .osp.
//
// O .osp e o arquivo do projeto do Outfit Studio: um <SliderSetInfo> com um ou
// mais <SliderSet>, e dentro de cada um os <Slider> na ordem em que o programa
// os mostra. Reordenar a lista na tela so vira ordem de verdade quando esses
// blocos trocam de lugar aqui.
//
// O trabalho e feito no TEXTO, e nao por um parser que reescreva o arquivo.
// Serializar de volta reformataria tudo -- indentacao, ordem de atributo, aspas,
// BOM -- e produziria um diff enorme num arquivo que e do usuario e que outros
// programas tambem leem. Aqui so os blocos mudam de lugar: cada byte que nao
// pertence a um <Slider> sai exatamente como entrou.

// Os nomes dos sliders de um conjunto, na ordem em que estao no arquivo.
// Vazio se o conjunto nao existir.
std::vector<std::string> ReadOspSliderOrder(const std::string& xml, const std::string& setName);

// Devolve o texto com os blocos <Slider> do conjunto na ordem pedida.
//
// Devolve VAZIO quando nao da para fazer com seguranca -- conjunto ausente,
// ordem que nao e uma permutacao exata dos sliders que estao la, arquivo em uma
// linha so. Recusar e a resposta certa: este arquivo e o trabalho do usuario, e
// um palpite aqui o corrompe em silencio.
std::string ReorderOspSliders(const std::string& xml, const std::string& setName,
							  const std::vector<std::string>& order);

// Procura o UNICO SliderSet cujo conjunto de nomes coincide exatamente com a
// ordem pedida e reordena esse conjunto. Devolve vazio se nenhum ou mais de um
// conjunto combinar: nesse caso escolher seria um palpite sobre o projeto do
// usuario. `matchedSet`, quando fornecido, recebe o nome do conjunto escolhido.
std::string ReorderUniqueMatchingOspSliderSet(const std::string& xml,
										   const std::vector<std::string>& order,
										   std::string* matchedSet = nullptr);
