@echo off

set compile_flags= -nologo /Zi /FC
set link_flags= -opt:ref -incremental:no /Debug:fastlink

if not exist build mkdir build
pushd build

start /b /wait "" "rc.exe" /nologo -fo ./mnotify.res ../src/mnotify.rc
start /b /wait "" "cl.exe" %compile_flags% ../src/mnotify.c mnotify.res /link %link_flags% /SUBSYSTEM:WINDOWS /out:mnotify.exe

if not exist "mnotify.ini" copy "..\\mnotify.ini"
if not exist "mnotify_logo.png" copy "..\\res\\mnotify_logo.png"
popd