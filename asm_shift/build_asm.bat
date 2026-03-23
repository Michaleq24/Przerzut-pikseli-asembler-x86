@echo off
rem Pełna ścieżka do MASM
"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\ml64.exe" /c /Fo"%~dp0\..\$(Configuration)\avx_worker.obj" "%~dp0\avx_worker.asm"