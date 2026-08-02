@echo off
REM Compila e roda o exe de testes. Rodar a partir da raiz do repositorio.
setlocal enabledelayedexpansion

if not exist tests\test_main.cpp (
    echo ERRO: rode a partir da raiz do repositorio.
    exit /b 1
)

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo ERRO: vcvars64.bat falhou.
    exit /b 1
)

if not exist build\tests mkdir build\tests

REM Fontes de teste, mais todo modulo de src que nao seja o proxy.
REM proxy.cpp fica de fora: define DllMain e os exports, testados via LoadLibrary.
set SRC=
for %%f in (tests\*.cpp) do set SRC=!SRC! %%f
for %%d in (src\core src\win32 src\features) do (
    if exist %%d\*.cpp for %%f in (%%d\*.cpp) do set SRC=!SRC! %%f
)
if exist src\xrcmap.cpp set SRC=!SRC! src\xrcmap.cpp

cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /DUNICODE /D_UNICODE /DBSOS_TESTS ^
   /I src /Fo:build\tests\ /Fe:build\tests\tests.exe ^
   !SRC! ^
   /link user32.lib gdi32.lib comctl32.lib shlwapi.lib
if errorlevel 1 (
    echo.
    echo ERRO: compilacao dos testes falhou.
    exit /b 1
)

build\tests\tests.exe
exit /b %errorlevel%
