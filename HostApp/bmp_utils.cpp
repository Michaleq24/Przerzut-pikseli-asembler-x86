// bmp_utils.cpp

// Nag³ówki
#include "bmp_utils.h"
#include <fstream>
#include <iostream>
#pragma pack(push,1) // Aby kompilator nie dodawa³ pustych bajdów miedzy polami struktury - poprawny odczyt bitmapy

// Sygnatura bitmapy oraz wskaŸnik rozpoczêcia pikseli bfOffBits
struct BMPHeader {
	uint16_t bfType;
	uint32_t bfSize;
	uint16_t bfReserved1;
	uint16_t bfReserved2;
	uint32_t bfOffBits;
};

// Wymiary obrazu i g³êbia koloru
struct DIBHeader {
	uint32_t biSize;
	int32_t biWidth;
	int32_t biHeight;
	uint16_t biPlanes;
	uint16_t biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	int32_t biXPelsPerMeter;
	int32_t biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
};
#pragma pack(pop)

// Funkcja loadera bitmapy
bool LoadBMP(const char* filename, BMPImage& out) {
	std::ifstream f(filename, std::ios::binary);
	if (!f) return false;
	BMPHeader bh; DIBHeader dh;
	f.read((char*)&bh, sizeof(bh));
	if (bh.bfType != 0x4D42) return false; // ASCII: 'BM'
	f.read((char*)&dh, sizeof(dh));

	// Walidacja formatu
	if (dh.biBitCount != 32) {
		std::cerr << "Program zaklada 32bpp BMP (BGRA).\n";
		return false;
	}
	out.width = dh.biWidth;

	// Obs³uga ujemnej wysokoœci (standard BMP: ujemna = top-down, dodatnia = bottom-up)
	out.height = abs(dh.biHeight);
	out.bitsPerPixel = dh.biBitCount;

	// Alokacja pamiêci w wektorze
	size_t sz = (size_t)out.width * out.height * 4;
	out.pixels.resize(sz);

	// Przeskok do danych pikseli
	f.seekg(bh.bfOffBits, std::ios::beg);

	// Wczytanie surowych danych do wektora
	f.read((char*)out.pixels.data(), sz);
	return true;
}

// Funkcja do zapisu wynikowej bitmapy po przetworzeniu przez ASM/C
bool SaveBMP(const char* filename, const BMPImage& img) {
	std::ofstream f(filename, std::ios::binary);
	if (!f) return false;
	BMPHeader bh = {};
	DIBHeader dh = {};
	size_t sz = (size_t)img.width * img.height * 4;
	bh.bfType = 0x4D42;
	bh.bfOffBits = sizeof(BMPHeader) + sizeof(DIBHeader);
	bh.bfSize = (uint32_t)(bh.bfOffBits + sz);
	dh.biSize = sizeof(DIBHeader);
	dh.biWidth = img.width;
	dh.biHeight = img.height;
	dh.biPlanes = 1;
	dh.biBitCount = 32;
	dh.biCompression = 0;
	dh.biSizeImage = (uint32_t)sz;

	// Zapis dla nowej bitmapy
	f.write((char*)&bh, sizeof(bh));
	f.write((char*)&dh, sizeof(dh));
	f.write((char*)img.pixels.data(), sz);
	return true;
}