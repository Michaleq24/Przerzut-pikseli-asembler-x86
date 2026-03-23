# Przerzut-pikseli-asembler-x86
Program ze wstawką asemblerową stworzony na potrzeby zaliczenia przedmiotu JA.
•	implementacja w języku C — z optymalizacjami i podziałem na wątki,
•	implementacja w assemblerze — jądro przeniesione do MASM (AVX2), z wielowątkowym wrapperem (C) wywołującym wektorowy kod asm.
Program przenosi piksele bitmapy o podaną przez użytkownika wartość, porównując czasy wykonania operacji dla C i asm.
Przykładowe uruchomienie programu (exe, bitmapa, piksele, wątki): HostApp.exe test.bmp 2500 4
