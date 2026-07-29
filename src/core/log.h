#pragma once

// Log opcional em %TEMP%\BSOSImprovements_<tag>.log, desligado por padrao.
// Quando desligado, LogF retorna sem nem formatar a mensagem.
//
// O tag separa os arquivos por aplicacao. BodySlide e Outfit Studio rodam ao
// mesmo tempo no uso normal, e um arquivo unico faria o segundo a subir
// truncar o log do primeiro.
void LogInit(bool enabled, const wchar_t* tag);
void LogF(const char* fmt, ...);
