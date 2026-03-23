// bmp_utils.h
#pragma once
#include <cstdint>
#include <vector>


struct BMPImage {
	int width;
	int height;
	int bitsPerPixel;
	std::vector<uint8_t> pixels; // BGRA 32bpp
};


bool LoadBMP(const char* filename, BMPImage& out);
bool SaveBMP(const char* filename, const BMPImage& img);