#include "utf8.h"

#include <stdlib.h>
#include <string.h>

wchar_t* utf8_to_wide_alloc(const char* utf8_text)
{
    int wide_length = 0;
    wchar_t* result = NULL;

    if (!utf8_text) {
        return NULL;
    }

    wide_length = MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, NULL, 0);
    if (wide_length <= 0) {
        return NULL;
    }

    result = (wchar_t*)calloc((size_t)wide_length, sizeof(wchar_t));
    if (!result) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, result, wide_length) <= 0) {
        free(result);
        return NULL;
    }
    return result;
}

char* wide_to_utf8_alloc(const wchar_t* wide_text)
{
    int utf8_length = 0;
    char* result = NULL;

    if (!wide_text) {
        return NULL;
    }

    utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, NULL, 0, NULL, NULL);
    if (utf8_length <= 0) {
        return NULL;
    }

    result = (char*)calloc((size_t)utf8_length, sizeof(char));
    if (!result) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, result, utf8_length, NULL, NULL) <= 0) {
        free(result);
        return NULL;
    }
    return result;
}

void utf8_trim_trailing_newlines(char* text)
{
    size_t length = 0;
    if (!text) {
        return;
    }

    length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[length - 1] = '\0';
        --length;
    }
}

void safe_wcs_copy(wchar_t* destination, size_t destination_count, const wchar_t* source)
{
    if (!destination || destination_count == 0) {
        return;
    }
    if (!source) {
        destination[0] = L'\0';
        return;
    }

    wcsncpy_s(destination, destination_count, source, _TRUNCATE);
}
