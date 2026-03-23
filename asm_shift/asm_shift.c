// asm_shift.c

// Nag³ówki
#include "asm_shift.h"
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Deklaracja zewnêtrznej funkcji z pliku avx_worker.asm.
void avx_rotate_rows(unsigned char* src, unsigned char* dst, int rowBytes,
    int rowStart, int rowEnd, int shiftBytes);

typedef struct {
    unsigned char* src; // WskaŸnik na pocz¹tek ca³ego obrazu Ÿród³owego
    unsigned char* dst; // WskaŸnik na pocz¹tek FRAGMENTU bufora wynikowego - jest to ma³y bufor przydzielony dla danego w¹tku
    int rowStart;       // Indeks pierwszego wiersza, który ten w¹tek ma przetworzyæ (np. 0)
    int rowEnd;         // Indeks wiersza koñcowego dla danego w¹tku (np. 100)
    int rowBytes;       // Szerokoœæ jednego wiersza w bajtach (width * bytesPerPixel)
    int shiftBytes;     // O ile bajtów przesuwamy w prawo (+ jak w prawo, - jak w lewo)
    int threadIndex;    // Numer w¹tku (dla wyœwietlenia w konsoli)
} ThreadArg;

DWORD WINAPI thread_fn(LPVOID lp) {
    ThreadArg* a = (ThreadArg*)lp;

   printf("[ASM Watek %d] rows %d..%d\n",  // Dla wyœwietlenia w konsoli
    a->threadIndex, a->rowStart, a->rowEnd);

    // Wywo³anie ASM.
    // Zmiana wzglêdem wersji z C: 'dst' wskazuje teraz na ma³y bufor w¹tku, a nie na g³ówny bufor z bitmap¹.
    // Kod w asemblerze dzia³a w trybie: Src -> Ma³y Bufor (okreœlony tutaj jako dst) -> Src (Copy-Back).
    avx_rotate_rows(a->src, a->dst, a->rowBytes, a->rowStart, a->rowEnd, a->shiftBytes);
    return 0;
}

__declspec(dllexport)
void shift_bitmap(unsigned char* pixels, int width, int height, int bpp, int shiftPixels, int numThreads){
    int bytesPerPixel = bpp / 8;          // Np. 32bpp -> 4 bajty
    int rowBytes = width * bytesPerPixel; // Szerokoœæ wiersza w bajtach (Stride)

    // Normalizacja przesuniêcia (modulo).
    // Zapobiega b³êdom gdy przesuniêcie wiêksze ni¿ szerokoœæ lub ujemne.
    int S = shiftPixels % width;
    if (S < 0) S += width;                    // Obs³uga ujemnego przesuniêcia (w lewo)
    int shiftBytes = S * bytesPerPixel;       // Przeliczenie pikseli na bajty
    size_t total = (size_t)rowBytes * height; // Ca³kowity rozmiar obrazu w bajtach

    // Konfiguracja w¹tków
    int nThreads = numThreads;
    if (nThreads < 1) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        nThreads = (int)si.dwNumberOfProcessors; // Gdy podano 0 to w¹tków tyle ile ma procesor
    }

    printf("Uruchamianie ASM...\n");

    unsigned char* threadBuffers = (unsigned char*)malloc(nThreads * rowBytes);
    if (!threadBuffers) return;

    // Tablice na uchwyty w¹tków i ich argumenty
    HANDLE* threads = (HANDLE*)malloc(nThreads * sizeof(HANDLE));
    ThreadArg* args = (ThreadArg*)malloc(nThreads * sizeof(ThreadArg));

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

        args[t].src = pixels;   // Zród³o - g³ówny obraz

        // Ka¿dy w¹tek dostaje swój w³asny, ma³y kawa³ek pamiêci jako 'dst'.
        // ASM bêdzie pisa³ do tego ma³ego bufora i natychmiast kopiowa³ z niego do 'pixels'.
        args[t].dst = threadBuffers + (t * rowBytes);

        // Wype³nienie struktury argumentów
        args[t].rowStart = start;
        args[t].rowEnd = end;
        args[t].rowBytes = rowBytes;
        args[t].shiftBytes = shiftBytes;
        args[t].threadIndex = t;

        // Utworzenie w¹tku systemowego
        threads[t] = CreateThread(NULL, 0, thread_fn, &args[t], 0, NULL);
    }

    // G³ówny w¹tek zatrzymuje siê tutaj i czeka, a¿ wszystkie (nThreads) w¹tki zakoñcz¹ pracê.
    // TRUE - "czekaj na wszystkie", INFINITE - "bez limitu czasu"
    WaitForMultipleObjects(nThreads, threads, TRUE, INFINITE);
    for (int t = 0; t < nThreads; ++t) CloseHandle(threads[t]);

    // Zwolnienie pamiêci stosu
    free(threadBuffers);
    free(threads);
    free(args);
}