// main.cpp

// Nagłówki
#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <stdint.h>
#include "bmp_utils.h"

// Definicja wskaźnika na funkcję - shift_c/asm jako zmienne z adresami tych funkcji z plików DLL
using ShiftFunc = void(*)(uint8_t* pixels, int width, int height, int bpp, int shiftPixels, int numThreads);

int main(int argc, char* argv[]) {

    std::string input;
    int shiftPixels = 0;
    int numThreads = 0;

    // Parsowanie argumentów
    if (argc >= 2) input = argv[1];               // plik BMP
    if (argc >= 3) shiftPixels = atoi(argv[2]);   // liczba pikseli do przesunięcia
    if (argc >= 4) numThreads = atoi(argv[3]);    // liczba wątków

    // Obsługa alternatywnego wywołania startu programu
    if (argc < 2) {
        std::cout << "Podaj nazwe pliku BMP: ";
        std::getline(std::cin, input);
        std::cout << "Podaj liczbe pikseli do przesuniecia: ";
        std::cin >> shiftPixels;
        std::cout << "Podaj liczbe watkow (0 = auto): ";
        std::cin >> numThreads;
    }

    std::string outC = "out_c.bmp";
    std::string outASM = "out_asm.bmp";

    BMPImage img;
    if (!LoadBMP(input.c_str(), img)) {
        std::cerr << "Nie udalo sie wczytac BMP: " << input << "\n";
        return 1;
    }
    if (img.bitsPerPixel != 32) {
        std::cerr << "Program zaklada 32bpp BMP (BGRA). Twoje BMP ma " << img.bitsPerPixel << "bpp.\n";
        return 1;
    }

    // Ładowanie obu DLL ze sprawdzeniem czy istnieją
    HMODULE dllC = LoadLibraryA("c_shift.dll");
    HMODULE dllASM = LoadLibraryA("asm_shift.dll");
    if (!dllC || !dllASM) {
        std::cerr << "Nie udalo sie zaladowac DLL.\n";
        return 1;
    }

    // Pobranie adresu funkcji o nazwie "shift_bitmap" z biblioteki
    ShiftFunc shift_c = (ShiftFunc)GetProcAddress(dllC, "shift_bitmap");
    ShiftFunc shift_asm = (ShiftFunc)GetProcAddress(dllASM, "shift_bitmap");
    if (!shift_c || !shift_asm) {
        std::cerr << "Brak wymaganej funkcji w jednej z DLL.\n";
        return 1;
    }

    std::vector<uint8_t> bufC = img.pixels;
    std::vector<uint8_t> bufASM = img.pixels;

    auto t0 = std::chrono::high_resolution_clock::now();
    shift_c(bufC.data(), img.width, img.height, img.bitsPerPixel, shiftPixels, numThreads);
    auto t1 = std::chrono::high_resolution_clock::now();
    double msC = std::chrono::duration<double, std::milli>(t1 - t0).count();

    auto t2 = std::chrono::high_resolution_clock::now();
    shift_asm(bufASM.data(), img.width, img.height, img.bitsPerPixel, shiftPixels, numThreads);
    auto t3 = std::chrono::high_resolution_clock::now();
    double msASM = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // Do wyświetlenia czasów pracy w konsoli
    std::cout << "\nC version: " << msC << " ms\n";
    std::cout << "ASM version: " << msASM << " ms\n";
    std::cout << "Speedup (C / ASM): " << (msC / msASM) << "\n";

    // Zapis wynikow do nowych bitmap
    BMPImage outCImg = img; outCImg.pixels = std::move(bufC);
    BMPImage outASMImg = img; outASMImg.pixels = std::move(bufASM);
    SaveBMP(outC.c_str(), outCImg);
    SaveBMP(outASM.c_str(), outASMImg);

    // Zwolnienie DLL z pamięci procesu
    FreeLibrary(dllC);
    FreeLibrary(dllASM);
    return 0;
}