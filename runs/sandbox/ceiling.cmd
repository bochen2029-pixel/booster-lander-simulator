@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /fp:precise /W3 "C:\Booster_Lander_Simulator\runs\sandbox\ceiling.c" /Fe:"C:\Booster_Lander_Simulator\runs\sandbox\ceiling.exe" /Fo:"%TEMP%\ceiling_obj.obj"
