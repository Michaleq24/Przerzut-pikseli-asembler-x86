// c_shift.c

// Nag³ówki
#include "c_shift.h"
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Struktura argumentów dla pojedynczego w¹tku.
// S³u¿y jako paczka danych przekazywana do funkcji worker.
typedef struct {
    unsigned char* src; // WskaŸnik na pocz¹tek ca³ego obrazu Ÿród³owego
    unsigned char* dst; // WskaŸnik na pocz¹tek ca³ego bufora wynikowego
    int rowStart;       // Indeks pierwszego wiersza, który ten w¹tek ma przetworzyæ (np. 0)
    int rowEnd;         // Indeks wiersza koñcowego dla danego w¹tku (np. 100)
    int rowBytes;       // Szerokoœæ jednego wiersza w bajtach (width * bytesPerPixel)
    int shiftBytes;     // O ile bajtów przesuwamy w prawo (+ jak w prawo, - jak w lewo)
    int threadIndex;    // Numer w¹tku (dla wyœwietlenia w konsoli)
} ThreadArg;

// Funkcja worker wykonywana przez ka¿dy w¹tek na podanym fragmencie obrazu Ÿród³owego
DWORD WINAPI worker(LPVOID lp) {
    ThreadArg* a = (ThreadArg*)lp; // Rzutowanie wskaŸnika void* z powrotem na strukturê argumentów
    printf("[Watek %d] rows %d..%d\n", a->threadIndex, a->rowStart, a->rowEnd); // Dla wyœwietlenia w konsoli

    // Pêtla po przydzielonych wierszach (od rowStart do rowEnd)
    for (int r = a->rowStart; r < a->rowEnd; ++r) {
        // Obliczenie wskaŸnika na pocz¹tek bie¿¹cego wiersza w pamiêci Ÿród³owej
        // Adres = Baza + (NumerWiersza * SzerokoœæWiersza)
        unsigned char* rowSrc = a->src + (size_t)r * a->rowBytes;

        // Obliczenie wskaŸnika na to samo miejsce w buforze docelowym
        unsigned char* rowDst = a->dst + (size_t)r * a->rowBytes;

        // Lokalne zmienne
        int S = a->shiftBytes;  // Wartoœæ do przeniesienia
        int W = a->rowBytes;    // Ca³a wartoœæ - szerokoœæ obrazu (wiersza)
        int leftPart = W - S;   // D³ugoœæ czêœci, która zostanie przesuniêta w prawo

        // Przesuniêcie w prawo — ogon na pocz¹tek
        memcpy(rowDst, rowSrc + leftPart, S); // ¯ród³o to rowSrc+leftPart wiêc pocz¹tek wiersza
        memcpy(rowDst + S, rowSrc, leftPart); // ¯ród³o to rowSrc
    }
    return 0;
}

__declspec(dllexport) void shift_bitmap(unsigned char* pixels, int width, int height, int bpp, int shiftPixels, int numThreads) {
    int bytesPerPixel = bpp / 8;          // Np. 32bpp -> 4 bajty
    int rowBytes = width * bytesPerPixel; // Szerokoœæ wiersza w bajtach (Stride)

    // Normalizacja przesuniêcia (modulo).
    // Zapobiega b³êdom gdy przesuniêcie wiêksze ni¿ szerokoœæ lub ujemne.
    int S = shiftPixels % width;
    if (S < 0) S += width;                    // Obs³uga ujemnego przesuniêcia (w lewo)
    int shiftBytes = S * bytesPerPixel;       // Przeliczenie pikseli na bajty
    size_t total = (size_t)rowBytes * height; // Ca³kowity rozmiar obrazu w bajtach

    // Alokacja bufora pomocniczego
    // Tworzy kopiê ca³ego obrazu ('out'), aby w¹tki mog³y bezpiecznie pisaæ wynik nie niszcz¹c danych Ÿród³owych ('pixels'), z których inne w¹tki mog³yby czytaæ.
    unsigned char* out = (unsigned char*)malloc(total);
    if (!out) return;

    // Konfiguracja w¹tków
    int nThreads = numThreads;
    if (nThreads < 1) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        nThreads = (int)si.dwNumberOfProcessors; // Gdy podano 0 to w¹tków tyle ile ma procesor
    }

    printf("Uruchamianie C:\n");

    // Tablice na uchwyty w¹tków i ich argumenty
    HANDLE* threads = (HANDLE*)malloc(sizeof(HANDLE) * nThreads);
    ThreadArg* args = (ThreadArg*)malloc(sizeof(ThreadArg) * nThreads);

    // Podzia³ obrazu przez liczbê w¹tków.
    // baseRows: ile wierszy dostanie ka¿dy w¹tek (minimum)
    // remainder: reszta z dzielenia (te wiersze s¹ rozdawane po jednym na w¹tek licz¹c od pierwszego)
    int baseRows = height / nThreads;
    int remainder = height % nThreads;

    int currentRow = 0; // Licznik rozpoczêcia kolejnego bloku

    // Uruchomienie w¹tków
    for (int t = 0; t < nThreads; ++t) {
        // Sprawdzenie czy dany w¹tek dostaje dodatkowy wiersz z reszty.
        int rowsForThread = baseRows + (t < remainder ? 1 : 0);
        int start = currentRow;
        int end = start + rowsForThread;
        currentRow = end; // Przesuniêcie wskaŸnika dla nastêpnego w¹tku

        // Wype³nienie struktury argumentów
        args[t].src = pixels;  // Odczyt z orygina³u (src)
        args[t].dst = out;     // Zapis do kopii (dst)
        args[t].rowStart = start;
        args[t].rowEnd = end;
        args[t].rowBytes = rowBytes;
        args[t].shiftBytes = shiftBytes;
        args[t].threadIndex = t;

        // Utworzenie w¹tku systemowego
        threads[t] = CreateThread(NULL, 0, worker, &args[t], 0, NULL);
    }

    // G³ówny w¹tek zatrzymuje siê tutaj i czeka, a¿ wszystkie (nThreads) w¹tki zakoñcz¹ pracê.
    // TRUE - "czekaj na wszystkie", INFINITE - "bez limitu czasu"
    WaitForMultipleObjects(nThreads, threads, TRUE, INFINITE);
    for (int t = 0; t < nThreads; ++t) CloseHandle(threads[t]);

    // Wynik jest teraz w buforze 'out', musi zostaæ skopiowany z powrotem do 'pixels', aby bmp_utils widzia³ zmiany.
    memcpy(pixels, out, total);

    // Zwolnienie pamiêci stosu
    free(out);
    free(threads);
    free(args);
}