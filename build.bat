@echo off
REM Compila dist\msimg32.dll (x64). Rodar a partir da raiz do repositorio.
setlocal enabledelayedexpansion

if not exist src\proxy.cpp (
    echo ERRO: rode a partir da raiz do repositorio.
    exit /b 1
)

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo ERRO: vcvars64.bat falhou. Visual Studio 2022 Build Tools instalado?
    exit /b 1
)

if not exist dist mkdir dist
if not exist build\dll mkdir build\dll

set SRC=src\proxy.cpp
for %%d in (src\core src\win32 src\features) do (
    if exist %%d\*.cpp for %%f in (%%d\*.cpp) do set SRC=!SRC! %%f
)
if exist src\xrcmap.cpp set SRC=!SRC! src\xrcmap.cpp

REM /MT: CRT estatico, para o DLL nao depender de redistribuiveis do usuario.
cl /nologo /LD /O2 /MT /EHsc /std:c++17 /W4 /DUNICODE /D_UNICODE ^
   /I src /Fo:build\dll\ /Fe:dist\msimg32.dll ^
   !SRC! ^
   /link /DEF:src\msimg32.def user32.lib gdi32.lib comctl32.lib shlwapi.lib
if errorlevel 1 (
    echo.
    echo ERRO: compilacao falhou.
    exit /b 1
)

echo.
echo OK: dist\msimg32.dll
exit /b 0
