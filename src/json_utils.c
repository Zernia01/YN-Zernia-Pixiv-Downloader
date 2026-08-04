#include "json_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static const char* skip_ws(const char* p)
{
    while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        ++p;
    }
    return p;
}

static const char* skip_string_value(const char* p)
{
    if (!p || *p != '"') {
        return p;
    }
    ++p;
    while (*p) {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        ++p;
    }
    return p;
}

static const char* skip_braced_value(const char* p, char open_char, char close_char)
{
    int depth = 0;
    if (!p || *p != open_char) {
        return p;
    }

    while (*p) {
        if (*p == '"') {
            p = skip_string_value(p);
            continue;
        }
        if (*p == open_char) {
            ++depth;
        } else if (*p == close_char) {
            --depth;
            if (depth == 0) {
                return p + 1;
            }
        }
        ++p;
    }
    return p;
}

static const char* skip_json_value(const char* p)
{
    p = skip_ws(p);
    if (!p) {
        return NULL;
    }
    if (*p == '"') {
        return skip_string_value(p);
    }
    if (*p == '{') {
        return skip_braced_value(p, '{', '}');
    }
    if (*p == '[') {
        return skip_braced_value(p, '[', ']');
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        ++p;
    }
    return p;
}

static int hex_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static int parse_hex_quad(const char* text, unsigned int* value_out)
{
    unsigned int value = 0;
    int index = 0;

    if (!text || !value_out) {
        return 0;
    }

    for (index = 0; index < 4; ++index) {
        int digit = hex_digit_value(text[index]);
        if (digit < 0) {
            return 0;
        }
        value = (value << 4) | (unsigned int)digit;
    }

    *value_out = value;
    return 1;
}

static size_t append_utf8_codepoint(char* output, size_t output_size, size_t used, unsigned int codepoint)
{
    char encoded[4];
    size_t encoded_size = 0;

    if (!output || output_size == 0) {
        return used;
    }

    if (codepoint <= 0x7F) {
        encoded[0] = (char)codepoint;
        encoded_size = 1;
    } else if (codepoint <= 0x7FF) {
        encoded[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        encoded[1] = (char)(0x80 | (codepoint & 0x3F));
        encoded_size = 2;
    } else if (codepoint <= 0xFFFF) {
        encoded[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = (char)(0x80 | (codepoint & 0x3F));
        encoded_size = 3;
    } else {
        encoded[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
        encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = (char)(0x80 | (codepoint & 0x3F));
        encoded_size = 4;
    }

    if (used + encoded_size >= output_size) {
        return used;
    }

    memcpy(output + used, encoded, encoded_size);
    return used + encoded_size;
}

static int extract_json_string(const char* p, char* output, size_t output_size)
{
    size_t used = 0;
    if (!p || *p != '"' || !output || output_size == 0) {
        return 0;
    }

    ++p;
    while (*p && *p != '"') {
        char value = *p;
        if (*p == '\\' && p[1] != '\0') {
            ++p;
            value = *p;
            switch (value) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            case 'u': {
                unsigned int codepoint = 0;

                if (!parse_hex_quad(p + 1, &codepoint)) {
                    value = '?';
                    break;
                }

                p += 4;
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && p[1] == '\\' && p[2] == 'u') {
                    unsigned int low_surrogate = 0;
                    if (parse_hex_quad(p + 3, &low_surrogate) && low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                        codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low_surrogate - 0xDC00));
                        p += 6;
                    }
                }

                used = append_utf8_codepoint(output, output_size, used, codepoint);
                ++p;
                continue;
            }
            default:
                break;
            }
        }
        if (used + 1 < output_size) {
            output[used++] = value;
        }
        ++p;
    }
    output[used] = '\0';
    return 1;
}

static int find_key_in_object(const char* json, const char* key, const char** value_start, const char** value_end)
{
    const char* p = skip_ws(json);
    char found_key[128];

    if (!p || *p != '{') {
        return 0;
    }
    ++p;

    for (;;) {
        p = skip_ws(p);
        if (!p || *p == '}') {
            return 0;
        }

        if (*p != '"') {
            return 0;
        }
        if (!extract_json_string(p, found_key, sizeof(found_key))) {
            return 0;
        }
        p = skip_string_value(p);
        p = skip_ws(p);
        if (!p || *p != ':') {
            return 0;
        }
        ++p;
        p = skip_ws(p);
        if (!p) {
            return 0;
        }

        if (strcmp(found_key, key) == 0) {
            *value_start = p;
            *value_end = skip_json_value(p);
            return 1;
        }

        p = skip_json_value(p);
        p = skip_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            return 0;
        }
    }
}

char* read_text_file_utf8(const wchar_t* path)
{
    FILE* file = NULL;
    long file_size = 0;
    char* buffer = NULL;

    if (_wfopen_s(&file, path, L"rb") != 0 || !file) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    file_size = ftell(file);
    rewind(file);

    buffer = (char*)calloc((size_t)file_size + 1, sizeof(char));
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    if (file_size > 0) {
        fread(buffer, 1, (size_t)file_size, file);
    }
    buffer[file_size] = '\0';
    fclose(file);
    return buffer;
}

int write_text_file_utf8(const wchar_t* path, const char* text)
{
    FILE* file = NULL;
    if (_wfopen_s(&file, path, L"wb") != 0 || !file) {
        return 0;
    }
    if (text && *text) {
        fwrite(text, 1, strlen(text), file);
    }
    fclose(file);
    return 1;
}

int json_get_string(const char* json, const char* key, char* output, size_t output_size)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    (void)value_end;

    if (!find_key_in_object(json, key, &value_start, &value_end)) {
        return 0;
    }
    value_start = skip_ws(value_start);
    if (!value_start || *value_start != '"') {
        return 0;
    }
    return extract_json_string(value_start, output, output_size);
}

char* json_get_string_alloc(const char* json, const char* key)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t capacity = 0;
    char* output = NULL;

    if (!find_key_in_object(json, key, &value_start, &value_end)) {
        return NULL;
    }
    value_start = skip_ws(value_start);
    if (!value_start || *value_start != '"' || !value_end || value_end <= value_start) {
        return NULL;
    }

    capacity = (size_t)(value_end - value_start) + 1;
    output = (char*)calloc(capacity, sizeof(char));
    if (!output) {
        return NULL;
    }
    if (!extract_json_string(value_start, output, capacity)) {
        free(output);
        return NULL;
    }
    return output;
}

int json_get_int(const char* json, const char* key, int default_value)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    char buffer[64];
    size_t length = 0;

    if (!find_key_in_object(json, key, &value_start, &value_end)) {
        return default_value;
    }

    value_start = skip_ws(value_start);
    length = (size_t)(value_end - value_start);
    if (length >= sizeof(buffer)) {
        length = sizeof(buffer) - 1;
    }
    memcpy(buffer, value_start, length);
    buffer[length] = '\0';
    return atoi(buffer);
}

int json_get_bool(const char* json, const char* key, int default_value)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t length = 0;

    if (!find_key_in_object(json, key, &value_start, &value_end)) {
        return default_value;
    }

    value_start = skip_ws(value_start);
    length = (size_t)(value_end - value_start);
    if (length >= 4 && strncmp(value_start, "true", 4) == 0) {
        return 1;
    }
    if (length >= 5 && strncmp(value_start, "false", 5) == 0) {
        return 0;
    }
    return default_value;
}

int json_get_nested_string(const char* json, const char* object_key, const char* key, char* output, size_t output_size)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t object_length = 0;
    char* object_text = NULL;
    int found = 0;

    if (!find_key_in_object(json, object_key, &value_start, &value_end)) {
        return 0;
    }
    value_start = skip_ws(value_start);
    if (!value_start || *value_start != '{') {
        return 0;
    }

    object_length = (size_t)(value_end - value_start);
    object_text = (char*)calloc(object_length + 1, sizeof(char));
    if (!object_text) {
        return 0;
    }
    memcpy(object_text, value_start, object_length);
    object_text[object_length] = '\0';

    found = json_get_string(object_text, key, output, output_size);
    free(object_text);
    return found;
}

char* json_extract_object_alloc(const char* json, const char* key)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t object_length = 0;
    char* object_text = NULL;

    if (!find_key_in_object(json, key, &value_start, &value_end)) {
        return NULL;
    }
    value_start = skip_ws(value_start);
    if (!value_start || *value_start != '{') {
        return NULL;
    }

    object_length = (size_t)(value_end - value_start);
    object_text = (char*)calloc(object_length + 1, sizeof(char));
    if (!object_text) {
        return NULL;
    }
    memcpy(object_text, value_start, object_length);
    object_text[object_length] = '\0';
    return object_text;
}

char* json_extract_array_alloc(const char* json, const char* key)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t length = 0;
    char* output = NULL;

    if (!find_key_in_object(json, key, &value_start, &value_end) || !value_start || *value_start != '[') {
        return NULL;
    }
    length = (size_t)(value_end - value_start);
    output = (char*)calloc(length + 1, sizeof(char));
    if (!output) {
        return NULL;
    }
    memcpy(output, value_start, length);
    output[length] = '\0';
    return output;
}

int json_collect_top_level_strings(const char* json, JsonStringPair* pairs, int max_pairs)
{
    const char* p = skip_ws(json);
    int count = 0;

    if (!p || *p != '{') {
        return 0;
    }
    ++p;

    while (*p && count < max_pairs) {
        char key[128];
        char value[1024];

        p = skip_ws(p);
        if (*p == '}') {
            break;
        }
        if (*p != '"') {
            break;
        }
        if (!extract_json_string(p, key, sizeof(key))) {
            break;
        }
        p = skip_string_value(p);
        p = skip_ws(p);
        if (*p != ':') {
            break;
        }
        ++p;
        p = skip_ws(p);

        if (strcmp(key, "_meta") != 0 && *p == '"' && extract_json_string(p, value, sizeof(value))) {
            strncpy_s(pairs[count].key, sizeof(pairs[count].key), key, _TRUNCATE);
            strncpy_s(pairs[count].value, sizeof(pairs[count].value), value, _TRUNCATE);
            ++count;
        }

        p = skip_json_value(p);
        p = skip_ws(p);
        if (*p == ',') {
            ++p;
        }
    }

    return count;
}

void json_escape_string(const char* source, char* destination, size_t destination_size)
{
    size_t used = 0;
    const char* p = source;

    if (!destination || destination_size == 0) {
        return;
    }
    if (!source) {
        destination[0] = '\0';
        return;
    }

    while (*p && used + 1 < destination_size) {
        const char* replacement = NULL;
        char single[2] = { 0, 0 };

        switch (*p) {
        case '\\':
            replacement = "\\\\";
            break;
        case '"':
            replacement = "\\\"";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            single[0] = *p;
            replacement = single;
            break;
        }

        while (*replacement && used + 1 < destination_size) {
            destination[used++] = *replacement++;
        }
        ++p;
    }
    destination[used] = '\0';
}
