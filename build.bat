@echo off
rem Builds ArrowheadUSBIO.dll from source with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
rem Put llvm-mingw's bin folder on PATH first, then run this script.
cd /d "%~dp0"
x86_64-w64-mingw32-clang -O2 -shared -Wall -Wextra -nostdlib -nostartfiles -Wl,--entry=DllMain,--no-insert-timestamp -o ArrowheadUSBIO.dll src\arrowhead_volume_proxy.c -lkernel32
