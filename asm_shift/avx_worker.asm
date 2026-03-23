; =====================================================================
; avx_worker.asm
; =====================================================================

; Ustawienie opcji asemblera: rozróżnianie wielkości liter (np. Label != label).
; Jest to konieczne przy współpracy z językiem C/C++, który jest case-sensitive.
OPTION CASEMAP:NONE

; Dyrektywa PUBLIC eksportuje symbol 'avx_rotate_rows' na zewnątrz pliku obj.
; Dzięki temu linker (łączący pliki .obj w .dll/.exe) widzi tę funkcję i kod C może ją wywołać.
PUBLIC avx_rotate_rows

; Początek sekcji kodu wykonywalnego (.text). Tutaj znajdują się instrukcje procesora.
.CODE

; Deklaracja procedury.
avx_rotate_rows PROC
    ; =================================================================
    ; KONWENCJA WOŁANIA (Windows x64 ABI) I ARGUMENTY
    ; =================================================================
    ; Argumenty 1-4 są w rejestrach, reszta na stosie.
    ; Argument 1 (RCX): BaseSRC - Wskaźnik na początek dużej bitmapy w pamięci RAM.
    ; Argument 2 (RDX): BaseDST - Wskaźnik na MAŁY bufor roboczy (Scratchpad) - bufor ten mieści się w Cache L1 procesora.
    ; Argument 3 (R8):  rowBytes - Szerokość wiersza w bajtach (stride).
    ; Argument 4 (R9):  rowStart - Numer wiersza, od którego zaczynamy pracę.
    ; Argument 5 ([RBP+48]): rowEnd - Numer wiersza końcowego.
    ; Argument 6 ([RBP+56]): shiftBytes - O ile bajtów przesuwamy w prawo.

    ; --- PROLOG FUNKCJI ---
    ; Zapisujemy stary wskaźnik bazowy stosu (RBP) wywołującego.
    push    rbp

    ; Ustawiamy obecny wskaźnik stosu (RSP) jako nową bazę (RBP) dla naszej funkcji.
    ; Pozwala to bezpiecznie odwoływać się do argumentów na stosie i zmiennych lokalnych.
    mov     rbp, rsp

    ; --- ZABEZPIECZENIE REJESTRÓW (Callee-Saved) ---
    ; Zgodnie z konwencją Windows x64, funkcja musi przywrócić te rejestry do stanu pierwotnego przed powrotem. Jeśli je zmieniamy, musimy je najpierw zapisać na stosie.
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    ; --- ŁADOWANIE ARGUMENTÓW DO REJESTRÓW TRWAŁYCH ---
    ; Przenosimy rowBytes z R8 (rejestr ulotny/volatile) do R12 (rejestr trwały).
    mov     r12, r8

    ; Wczytujemy argumenty ze STOSU.
    ; Adres [rbp + 48] wynika z:
    ; 16 bajtów (stary RBP + adres powrotu) + 32 bajty (Shadow Space zarezerwowane przez kompilator C).
    ; Shadow Space to obszar, który funkcja wołająca rezerwuje dla wołanej.
    mov     r14d, DWORD PTR [rbp + 48]   ; Wczytaj rowEnd (32-bit int) do R14D.
    mov     r15d, DWORD PTR [rbp + 56]   ; Wczytaj shiftBytes (32-bit int) do R15D.

    ; --- OBLICZENIE LICZNIKA PĘTLI ---
    ; Obliczamy ile wierszy mamy przetworzyć: count = rowEnd - rowStart.
    mov     eax, r14d           ; Kopia rowEnd do EAX.
    sub     eax, r9d            ; Odejmij rowStart.
    jle     finish              ; Jeśli wynik <= 0 (brak pracy), skocz na koniec (zabezpieczenie).
    mov     r10d, eax           ; Zapisz liczbę wierszy w R10D (to będzie nasz licznik głównej pętli).

    ; --- PRZYGOTOWANIE WSKAŹNIKÓW (SETUP) ---
    ; Musimy ustawić wskaźnik źródłowy (SRC) na konkretny wiersz startowy w pamięci.
    mov     rax, r9             ; Kopia rowStart do RAX.
    imul    rax, r12            ; Mnożenie: offset = rowStart * rowBytes. Wynik 64-bitowy w RAX.
    
    add     rcx, rax            ; Dodajemy offset do bazy SRC. RCX wskazuje teraz na pierwszy piksel wiersza startowego.
    ; RDX wskazuje na początek małego bufora roboczego. Zawsze będziemy pisać do indeksu 0 tego bufora, niezależnie od numeru wiersza.
    
    mov     r13, rcx            ; R13 = Globalny wskaźnik SRC (Będzie inkrementowany co wiersz).
    mov     r14, rdx            ; R14 = Stały wskaźnik DST (Zawsze wskazuje na Scratchpad w Cache L1).

    ; Obliczenie punktu podziału wiersza (gdzie tniemy, żeby obrócić).
    ; leftPart = rowBytes - shiftBytes (długość części, która ląduje na końcu).
    mov     rbx, r12            ; Kopia rowBytes.
    sub     rbx, r15            ; Odejmij shiftBytes. RBX to teraz 'leftPart'.

    ; =================================================================
    ; GŁÓWNA PĘTLA PRZETWARZANIA WIERSZY
    ; =================================================================
row_loop_opt:
    ; Na początku każdego obrotu pętli resetujemy wskaźniki robocze.
    mov     rsi, r13            ; RSI = Źródło w RAM (przesuwa się po obrazie).
    mov     rdi, r14            ; RDI = Cel w Cache (zawsze ten sam mały bufor).

    ; ========================= 1: KOPIOWANIE OGONA (Tail) =========================
    ; Bierzemy końcówkę wiersza źródłowego i wstawiamy ją na POCZĄTEK bufora.
    ; Źródło: [rsi + leftPart]. Cel: [rdi].
    
    lea     rax, [rsi + rbx]    ; LEA oblicza adres: RAX = RSI + RBX (czyli src + leftPart).
    mov     rdx, rdi            ; RDX = Początek bufora docelowego.
    mov     rcx, r15            ; RCX = Licznik bajtów do skopiowania (shiftBytes).
    
    ; Decyzja: czy użyć szybkiej pętli wektorowej (128 bajtów), czy wolniejszej?
    cmp     rcx, 128
    jb      tail_block_32       ; Jeśli mniej niż 128 bajtów, idź do obsługi małych bloków.

tail_unrolled:
    ; --- PREFETCH (Wstępne pobieranie) ---
    ; Procesor zaczyna ściągać dane pod adresem [RAX + 128] z RAM do Cache w tle, podczas gdy przetwarzane są obecne dane.
    prefetcht0 [rax + 128]

    ; --- KOPIOWANIE WEKTOROWE AVX (4 x 32 bajty = 128 bajtów) ---
    ; Wczytujemy 128 bajtów z pamięci do czterech rejestrów YMM (256-bitowych).
    ; Używamy vmovdqu (Unaligned), bo nie mamy gwarancji, że adresy są wyrównane do 32 bajtów.
    vmovdqu ymm0, YMMWORD PTR [rax]       ; Wczytaj 0-31 bajtów
    vmovdqu ymm1, YMMWORD PTR [rax + 32]  ; Wczytaj 32-63 bajtów
    vmovdqu ymm2, YMMWORD PTR [rax + 64]  ; Wczytaj 64-95 bajtów
    vmovdqu ymm3, YMMWORD PTR [rax + 96]  ; Wczytaj 96-127 bajtów
    
    ; Zapisujemy te dane do bufora roboczego.
    vmovdqu YMMWORD PTR [rdx], ymm0
    vmovdqu YMMWORD PTR [rdx + 32], ymm1
    vmovdqu YMMWORD PTR [rdx + 64], ymm2
    vmovdqu YMMWORD PTR [rdx + 96], ymm3

    ; Aktualizacja wskaźników i licznika.
    sub     rax, -128           ; Dodajemy 128 do RAX (źródło). (sub z liczbą ujemną jest czasem krótszy w kodzie maszynowym niż add).
    sub     rdx, -128           ; Dodajemy 128 do RDX (cel).
    sub     rcx, 128            ; Zmniejszamy licznik pozostałych bajtów.
    
    cmp     rcx, 128            ; Czy zostało jeszcze przynajmniej 128 bajtów?
    jae     tail_unrolled       ; Jeśli tak, skocz na początek pętli (Jump if Above or Equal).

    ; Obsługa resztek (32-bajtowe bloki)
tail_block_32:
    cmp     rcx, 32
    jb      tail_bytes          ; Jeśli zostało mniej niż 32 bajty, idź do kopiowania bajt po bajcie.
    
    vmovdqu ymm0, YMMWORD PTR [rax]
    vmovdqu YMMWORD PTR [rdx], ymm0
    
    add     rax, 32
    add     rdx, 32
    sub     rcx, 32
    jmp     tail_block_32

    ; Obsługa końcówki (bajt po bajcie) - dla precyzji co do 1 piksela/bajtu.
tail_bytes:
    test    rcx, rcx            ; Czy licznik jest zerem?
    jz      head_prepare        ; Jeśli tak, koniec tej sekcji, idź do "Head".
tail_byte_loop:
    mov     r8b, BYTE PTR [rax] ; Wczytaj 1 bajt do R8B (dolne 8 bitów R8).
    mov     BYTE PTR [rdx], r8b ; Zapisz 1 bajt.
    inc     rax                 ; Przesuń źródło.
    inc     rdx                 ; Przesuń cel.
    dec     rcx                 ; Zmniejsz licznik.
    jnz     tail_byte_loop      ; Powtarzaj, dopóki RCX != 0.

    ; ========================= 2: KOPIOWANIE GŁOWY (Head) =========================
    ; Kopiujemy początek wiersza źródłowego za wstawiony wcześniej ogon. To dopełnia operację rotacji.
head_prepare:
    mov     rax, rsi            ; RAX = Początek wiersza źródłowego (src).
    lea     rdx, [rdi + r15]    ; RDX = Bufor docelowy + shiftBytes (miejsce tuż za wstawionym ogonem).
    mov     rcx, rbx            ; RCX = Licznik bajtów (leftPart).

    cmp     rcx, 128
    jb      head_block_32

    ; Ta sekcja jest analogiczna do tail_unrolled.
    ; Używamy prefetcht0 i 4x vmovdqu dla maksymalnej przepustowości.
head_unrolled:
    prefetcht0 [rax + 128]

    vmovdqu ymm0, YMMWORD PTR [rax]
    vmovdqu ymm1, YMMWORD PTR [rax + 32]
    vmovdqu ymm2, YMMWORD PTR [rax + 64]
    vmovdqu ymm3, YMMWORD PTR [rax + 96]

    vmovdqu YMMWORD PTR [rdx], ymm0
    vmovdqu YMMWORD PTR [rdx + 32], ymm1
    vmovdqu YMMWORD PTR [rdx + 64], ymm2
    vmovdqu YMMWORD PTR [rdx + 96], ymm3

    sub     rax, -128
    sub     rdx, -128
    sub     rcx, 128
    cmp     rcx, 128
    jae     head_unrolled

head_block_32:
    cmp     rcx, 32
    jb      head_bytes
    vmovdqu ymm0, YMMWORD PTR [rax]
    vmovdqu YMMWORD PTR [rdx], ymm0
    add     rax, 32
    add     rdx, 32
    sub     rcx, 32
    jmp     head_block_32

head_bytes:
    test    rcx, rcx
    jz      copy_back_start
head_byte_loop:
    mov     r8b, BYTE PTR [rax]
    mov     BYTE PTR [rdx], r8b
    inc     rax
    inc     rdx
    dec     rcx
    jnz     head_byte_loop

    ; ========================= 3: COPY BACK (Zapis Zwrotny) =========================
    ; Mamy teraz obrócony wiersz w R14 (mały bufor).
    ; Dane w R14 znajdują się w najszybszej pamięci Cache L1 procesora, bo przed chwilą je zapisaliśmy.
    ; Kopiujemy je z powrotem do R13 (oryginalny obraz w RAM).
copy_back_start:
    mov     rsi, r14            ; Źródło = Scratch Buffer (Cache L1).
    mov     rdi, r13            ; Cel = Oryginalny obraz (RAM).
    mov     rcx, r12            ; Licznik = Pełna szerokość wiersza (rowBytes).

    ; Główna pętla zapisu zwrotnego (rozwinięta x4)
    cmp     rcx, 128
    jb      cb_block_32

cb_unrolled:
    ; 1. Czytamy z Cache L1 (Błyskawicznie) do rejestrów AVX.
    vmovdqu ymm0, YMMWORD PTR [rsi]
    vmovdqu ymm1, YMMWORD PTR [rsi + 32]
    vmovdqu ymm2, YMMWORD PTR [rsi + 64]
    vmovdqu ymm3, YMMWORD PTR [rsi + 96]
    
    ; 2. Zapisujemy do RAM (RDI).
    ; Procesor widzi sekwencję zapisów i używa "Write Combining" - łączy je w jedną paczkę wysyłaną do RAM, co jest znacznie szybsze niż pojedyncze zapisy.
    vmovdqu YMMWORD PTR [rdi], ymm0
    vmovdqu YMMWORD PTR [rdi + 32], ymm1
    vmovdqu YMMWORD PTR [rdi + 64], ymm2
    vmovdqu YMMWORD PTR [rdi + 96], ymm3

    ; Przesuwamy wskaźniki
    sub     rsi, -128
    sub     rdi, -128
    sub     rcx, 128
    cmp     rcx, 128
    jae     cb_unrolled

cb_block_32:
    cmp     rcx, 32
    jb      cb_bytes
    vmovdqu ymm0, YMMWORD PTR [rsi]
    vmovdqu YMMWORD PTR [rdi], ymm0
    add     rsi, 32
    add     rdi, 32
    sub     rcx, 32
    jmp     cb_block_32

cb_bytes:
    test    rcx, rcx
    jz      next_row
cb_byte_loop:
    mov     al, BYTE PTR [rsi]
    mov     BYTE PTR [rdi], al
    inc     rsi
    inc     rdi
    dec     rcx
    jnz     cb_byte_loop

    ; --- PRZEJŚCIE DO NASTĘPNEGO WIERSZA ---
next_row:
    add     r13, r12            ; Przesuwamy wskaźnik SRC (R13) o jeden wiersz w dół w pamięci RAM.
    
    ; R14 ciągle wskazuje na początek tego samego, małego bufora (Scratchpad).
    ; Dzięki temu bufor ten nigdy nie opuszcza pamięci podręcznej (Cache L1). To oszczędza setki megabajtów transferu RAM.

    dec     r10d                ; Zmniejszamy licznik wierszy.
    jnz     row_loop_opt        ; Jeśli licznik != 0, skaczemy na początek pętli.

    ; --- EPILOG FUNKCJI ---
finish:
    vzeroupper                  ; Czyścimy górne 128 bitów wszystkich rejestrów YMM.
                                ; Jest to wymagane przy powrocie do kodu, który może używać instrukcji SSE (Legacy).

    ; Przywracamy rejestry, które zapisaliśmy na początku (w odwrotnej kolejności).
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx

    mov     rsp, rbp            ; Przywracamy wskaźnik stosu (usuwamy zmienne lokalne, jeśli były).
    pop     rbp                 ; Przywracamy stary wskaźnik bazowy.
    ret                         ; Powrót z procedury (do kodu C).

avx_rotate_rows ENDP
END