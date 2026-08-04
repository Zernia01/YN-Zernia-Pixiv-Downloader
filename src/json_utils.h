#pragma once

#include <stddef.h>

typedef struct JsonStringPair {
    char key[128];
    char value[1024];
} JsonStringPair;

char* read_text_file_utf8(const wchar_t* path);
int write_text_file_utf8(const wchar_t* path, const char* text);
int json_get_string(const char* json, const char* key, char* output, size_t output_size);
char* json_get_string_alloc(const char* json, const char* key);
int json_get_int(const char* json, const char* key, int default_value);
int json_get_bool(const char* json, const char* key, int default_value);
int json_get_nested_string(const char* json, const char* object_key, const char* key, char* output, size_t output_size);
char* json_extract_object_alloc(const char* json, const char* key);
char* json_extract_array_alloc(const char* json, const char* key);
int json_collect_top_level_strings(const char* json, JsonStringPair* pairs, int max_pairs);
void json_escape_string(const char* source, char* destination, size_t destination_size);
