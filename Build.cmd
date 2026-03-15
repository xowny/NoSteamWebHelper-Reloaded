@echo off
setlocal

set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "TESTS=%ROOT%tests"
set "BUILD=%ROOT%build"
set "OBJ=%BUILD%\obj"
set "BIN=%BUILD%\bin"
set "RELEASE=%ROOT%release\steam"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do (
        set "VCVARS=%%I"
    )
)

if not defined VCVARS (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if not defined VCVARS (
    echo Could not locate vcvars64.bat.
    echo Install Visual Studio Build Tools with the x64 C/C++ toolchain.
    exit /b 1
)

call "%VCVARS%" >nul

if exist "%BUILD%" rd /q /s "%BUILD%"
if not exist "%RELEASE%" md "%RELEASE%"
md "%OBJ%" "%BIN%"

cl /nologo /W4 /WX /O1 /MT /DUNICODE /D_UNICODE /TC /Fo"%OBJ%\\" /c "%SRC%\State.c" "%SRC%\Library.c" || exit /b 1
lib /nologo /def:"%SRC%\umpdc.def" /machine:x64 /name:umpdc.dll /out:"%BIN%\umpdc_forward.lib" || exit /b 1
link /nologo /DLL /OUT:"%BIN%\umpdc.dll" "%OBJ%\Library.obj" "%OBJ%\State.obj" "%BIN%\umpdc_forward.exp" user32.lib advapi32.lib shell32.lib kernel32.lib || exit /b 1
copy /y "%SystemRoot%\System32\umpdc.dll" "%BIN%\umpdc_system.dll" >nul || exit /b 1
copy /y "%BIN%\umpdc.dll" "%RELEASE%\umpdc.dll" >nul || exit /b 1
copy /y "%BIN%\umpdc_system.dll" "%RELEASE%\umpdc_system.dll" >nul || exit /b 1

cl /nologo /W4 /WX /O1 /MT /DUNICODE /D_UNICODE /TC /Fo"%OBJ%\\StateTests.obj" /c "%TESTS%\StateTests.c" || exit /b 1
link /nologo /OUT:"%BIN%\StateTests.exe" "%OBJ%\StateTests.obj" "%OBJ%\State.obj" kernel32.lib || exit /b 1

echo Build complete:
echo   %BIN%\umpdc.dll
echo   %BIN%\umpdc_system.dll
echo   %BIN%\StateTests.exe
echo Release payload refreshed:
echo   %RELEASE%
