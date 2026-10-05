@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [!] vswhere not found. & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT ( echo [!] VS not found. & exit /b 1 )

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo [!] vcvars64 failed. & exit /b 1 )

pushd "%~dp0"
if not exist out mkdir out

if not exist "%~dp0imgui\imgui.cpp" (
    echo [!] ImGui submodule missing — run: git submodule update --init
    popd
    endlocal
    exit /b 1
)

set I=%~dp0imgui

rem xen-tool links against a D3D11 / Win32 GUI app; /SUBSYSTEM:WINDOWS so no
rem console pops up when the user launches it while the target game is focused.
rem ../xen-dumper is used as an include path so we can share XenReader.h
rem and XenPlatform.h without duplicating source.
set D=%~dp0..\xen-dumper

(
echo /nologo /std:c++17 /EHsc /W3 /O2 /MT /DNDEBUG
echo /DUNICODE /D_UNICODE /DNOMINMAX
echo /Gy /Oi /wd4267 /wd4244 /wd4100 /wd4189
echo /I"%~dp0"
echo /I"%I%"
echo /I"%I%\backends"
echo /I"%D%"
echo main.cpp
echo "%D%\XenReader.cpp"
echo "%I%\imgui.cpp"
echo "%I%\imgui_demo.cpp"
echo "%I%\imgui_draw.cpp"
echo "%I%\imgui_tables.cpp"
echo "%I%\imgui_widgets.cpp"
echo "%I%\backends\imgui_impl_win32.cpp"
echo "%I%\backends\imgui_impl_dx11.cpp"
echo /Fe:"%~dp0out\xen-tool.exe" /Fo:"%~dp0out\\" /Fd:"%~dp0out\xen-tool.pdb"
echo /link /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
echo kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib shlwapi.lib
echo d3d11.lib dxgi.lib dwmapi.lib winmm.lib avrt.lib dcomp.lib dcomp.lib
echo winhttp.lib
) > "%~dp0out\xen-tool.rsp"

echo [*] Compiling xen-tool.exe ...
cl @"%~dp0out\xen-tool.rsp"
if errorlevel 1 goto :fail

echo.
echo === Build complete: %~dp0out\xen-tool.exe ===
popd
endlocal
exit /b 0

:fail
echo [!] Build FAILED.
popd
endlocal
exit /b 1
