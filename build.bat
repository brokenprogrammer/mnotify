@echo off

where /Q cl.exe || (
    for /f "tokens=*" %%i in ('"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath') do set VisualStudio=%%i
    if "%VisualStudio%" equ "" (
        echo ERROR: Can't find visual studio installation
        exit /b 1
    )
    
    call "%VisualStudio%\VC\Auxiliary\Build\vcvarsall.bat" x64 || exit /b
)

if "%1" equ "debug" (
    set compile_flags= -nologo /Zi /FC
    set link_flags= -opt:ref -incremental:no /Debug:fastlink
) else (
    set compile_flags= -nologo /GL /O1 
    set link_flags= /LTCG -opt:ref -opt:icf -incremental:no
)

if not exist build mkdir build
pushd build

start /b /wait "" "rc.exe" /nologo -fo ./mnotify.res ../src/mnotify.rc
start /b /wait "" "cl.exe" %compile_flags% ../src/mnotify.c mnotify.res /link %link_flags% /SUBSYSTEM:WINDOWS /out:mnotify.exe

if not exist "mnotify.ini" copy "..\\mnotify.ini"
if not exist "mnotify_logo.png" copy "..\\res\\mnotify_logo.png"
popd