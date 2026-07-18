@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /fp:precise /W3 %1 /Fe:%2 /Fo:%TEMP%\p1obj.obj
