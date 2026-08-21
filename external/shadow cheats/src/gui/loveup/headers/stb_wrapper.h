// stb_wrapper.h
#pragma once
#include "security/xorstr.hpp"

// Объявления функций STB
extern "C" {
    unsigned char* stbi_load(char const* filename, int* x, int* y, int* channels_in_file, int desired_channels);
    unsigned char* stbi_load_from_memory (unsigned char const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void* retval_from_stbi_load);
    const char* stbi_failure_reason();
    // Добавьте другие функции, которые используете
}
