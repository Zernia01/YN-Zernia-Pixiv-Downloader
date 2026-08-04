#pragma once

#include <stddef.h>
#include <windows.h>

wchar_t* utf8_to_wide_alloc(const char* utf8_text);
char* wide_to_utf8_alloc(const wchar_t* wide_text);
void utf8_trim_trailing_newlines(char* text);
void safe_wcs_copy(wchar_t* destination, size_t destination_count, const wchar_t* source);
