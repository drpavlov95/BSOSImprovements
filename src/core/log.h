#pragma once

// Log opcional em %TEMP%\BSOSImprovements.log, desligado por padrao.
// Quando desligado, LogF retorna sem nem formatar a mensagem.
void LogInit(bool enabled);
void LogF(const char* fmt, ...);
