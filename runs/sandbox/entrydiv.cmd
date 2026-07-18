@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /O2 /fp:precise /W3 "C:\Booster_Lander_Simulator\runs\sandbox\entrydiv.c" /Fe:"C:\Booster_Lander_Simulator\runs\sandbox\entrydiv.exe" /Fo:"%TEMP%\entrydiv_obj.obj"
