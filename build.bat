@echo off

set compile_flags= -nologo /Zi /FC
set link_flags= -opt:ref -incremental:no /Debug:fastlink

if not exist build mkdir build
pushd build

start /b /wait "" "cl.exe" %compile_flags% ../src/main.c /link %link_flags% /out:mnotify.exe

popd