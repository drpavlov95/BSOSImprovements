#pragma once

#include <filesystem>

#include <windows.h>

namespace NativeProjectState {

using SaveSucceededCallback = void(*)(void* frameObject);

// Usa OutfitStudioFrame::SetPendingChanges(true), a mesma rotina usada pelas
// ferramentas nativas de edicao. Disponivel somente para a build reconhecida.
bool MarkPending(HWND frame);

// Le OutfitProject::mFileName diretamente da mesma instancia nativa usada por
// SaveProject(). Isso evita procurar o arquivo salvo enumerando todo o
// SliderSets (uma operacao particularmente cara atraves do VFS do MO2).
bool CurrentProjectFile(HWND frame, std::filesystem::path& path);

// Observa os dois pontos em que SaveProject/SaveProjectAs confirmam sucesso,
// imediatamente antes de limparem o dirty flag nativo. O callback roda na
// thread da UI e deve ser curto. Passar nullptr remove a observacao.
bool ObserveSuccessfulSaves(SaveSucceededCallback callback);

} // namespace NativeProjectState
