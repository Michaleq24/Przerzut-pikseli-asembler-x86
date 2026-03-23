#pragma once
#ifdef __cplusplus
extern "C" {
#endif

	__declspec(dllexport) void shift_bitmap(unsigned char* pixels, int width, int height, int bpp, int shiftPixels, int numThreads);

#ifdef __cplusplus
}
#endif