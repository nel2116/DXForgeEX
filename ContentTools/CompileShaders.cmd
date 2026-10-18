@echo off
rem Compiles the ContentTools compute shaders into C++ headers (.inc) with fxc.
rem Ported from Primal (Day 072), which adapted it from the DirectXTex library
rem (see https://github.com/microsoft/DirectXTex/blob/main/DirectXTex/Shaders/CompileShaders.cmd).
rem Invoked by the CompileShaders target in ContentTools.vcxproj, which skips it while the .inc files are up to date.
setlocal
set error=0

set FXOPTS=/nologo /Tcs_5_0 /WX /Ges /Zi /Zpc /Qstrip_reflect /Qstrip_debug

set PCFXC="%WindowsSDKExecutablePathX64%\fxc.exe"
if exist %PCFXC% goto continue

set PCFXC=fxc.exe

:continue
if not defined CompileShadersOutput set CompileShadersOutput=x64\Compiled
@if not exist "%CompileShadersOutput%" mkdir "%CompileShadersOutput%"
call :CompileShader EnvMapProcessing EquirectangularToCubeMapCS

endlocal & exit /b %error%

:CompileShader
set fxc=%PCFXC% "%1.hlsl" %FXOPTS% /E%2 "/Fh%CompileShadersOutput%\%1_%2.inc" "/Fd%CompileShadersOutput%\%1_%2.pdb" /Vn%1_%2
echo compiling %1_%2
%fxc% || set error=1
exit /b
