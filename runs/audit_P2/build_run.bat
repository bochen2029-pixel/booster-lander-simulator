@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Booster_Lander_Simulator\runs\audit_P2
cl /nologo /O2 /fp:precise %1 >build.log 2>&1
if errorlevel 1 (
  echo BUILD FAILED:
  type build.log
  exit /b 1
)
%~n1.exe
