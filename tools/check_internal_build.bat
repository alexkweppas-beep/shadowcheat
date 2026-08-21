@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )
cd /d "d:\‘lear\export\repo\internal"
if exist "%TEMP%\shadow_check" rmdir /s /q "%TEMP%\shadow_check"
mkdir "%TEMP%\shadow_check"
echo === dllmain.cpp + FeatureBinds.cpp ===
cl /c /std:c++20 /utf-8 /W3 /EHsc /I src /I src\imgui /DNDEBUG /D_WINDOWS /Fo"%TEMP%\shadow_check\\" src\dllmain.cpp src\Features\FeatureBinds.cpp
if errorlevel 1 ( echo FAILED: dllmain/FeatureBinds & exit /b 1 )
echo === ModelPreview.cpp ===
cl /c /std:c++20 /utf-8 /W3 /EHsc /I src /I src\imgui /DNDEBUG /D_WINDOWS /Fo"%TEMP%\shadow_check\\" src\Features\ModelPreview.cpp
if errorlevel 1 ( echo FAILED: ModelPreview & exit /b 1 )
echo === injector\main.cpp ===
cl /c /std:c++20 /utf-8 /W3 /EHsc /DNDEBUG /D_WINDOWS /Fo"%TEMP%\shadow_check\\" injector\main.cpp
if errorlevel 1 ( echo FAILED: injector & exit /b 1 )
echo ALL_OK
exit /b 0