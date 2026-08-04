#include "locale.h"

#include "json_utils.h"
#include "utf8.h"

#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "shlwapi.lib")

static void append_locale_entries_from_json(LocaleBundle* bundle, const char* json)
{
    JsonStringPair pairs[MAX_LOCALE_PAIRS];
    int pair_count = 0;
    int index = 0;

    if (!bundle || !json) {
        return;
    }

    pair_count = json_collect_top_level_strings(json, pairs, MAX_LOCALE_PAIRS);
    for (index = 0; index < pair_count && bundle->entry_count < MAX_LOCALE_PAIRS; ++index) {
        wchar_t* wide_key = utf8_to_wide_alloc(pairs[index].key);
        wchar_t* wide_value = utf8_to_wide_alloc(pairs[index].value);
        int existing = -1;
        int search = 0;

        if (!wide_key || !wide_value) {
            free(wide_key);
            free(wide_value);
            continue;
        }

        for (search = 0; search < bundle->entry_count; ++search) {
            if (wcscmp(bundle->entries[search].key, wide_key) == 0) {
                existing = search;
                break;
            }
        }

        if (existing >= 0) {
            safe_wcs_copy(bundle->entries[existing].value, 512, wide_value);
        } else {
            safe_wcs_copy(bundle->entries[bundle->entry_count].key, 128, wide_key);
            safe_wcs_copy(bundle->entries[bundle->entry_count].value, 512, wide_value);
            ++bundle->entry_count;
        }

        free(wide_key);
        free(wide_value);
    }
}

int locale_list_languages(const AppPaths* paths, LanguageItem* items, int max_items)
{
    wchar_t search_pattern[PATH_BUFFER_COUNT];
    WIN32_FIND_DATAW find_data;
    HANDLE handle = INVALID_HANDLE_VALUE;
    int count = 0;

    if (!paths || !items || max_items <= 0) {
        return 0;
    }

    safe_wcs_copy(search_pattern, PATH_BUFFER_COUNT, paths->locales_dir);
    PathAppendW(search_pattern, L"*.json");

    handle = FindFirstFileW(search_pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        wchar_t full_path[PATH_BUFFER_COUNT];
        wchar_t file_name[PATH_BUFFER_COUNT];
        char* json = NULL;
        char display_name_utf8[256];
        wchar_t* display_name_wide = NULL;

        if (count >= max_items) {
            break;
        }
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        safe_wcs_copy(file_name, PATH_BUFFER_COUNT, find_data.cFileName);
        PathRemoveExtensionW(file_name);

        safe_wcs_copy(full_path, PATH_BUFFER_COUNT, paths->locales_dir);
        PathAppendW(full_path, find_data.cFileName);

        json = read_text_file_utf8(full_path);
        if (!json) {
            continue;
        }

        if (!json_get_nested_string(json, "_meta", "language_name", display_name_utf8, sizeof(display_name_utf8))) {
            strncpy_s(display_name_utf8, sizeof(display_name_utf8), "unknown", _TRUNCATE);
        }

        display_name_wide = utf8_to_wide_alloc(display_name_utf8);
        safe_wcs_copy(items[count].code, 32, file_name);
        safe_wcs_copy(items[count].display_name, 64, display_name_wide ? display_name_wide : file_name);

        free(display_name_wide);
        free(json);
        ++count;
    } while (FindNextFileW(handle, &find_data));

    FindClose(handle);
    return count;
}

int locale_load_bundle(const AppPaths* paths, const wchar_t* code, LocaleBundle* bundle)
{
    wchar_t base_path[PATH_BUFFER_COUNT];
    wchar_t selected_path[PATH_BUFFER_COUNT];
    char* base_json = NULL;
    char* selected_json = NULL;

    if (!paths || !bundle) {
        return 0;
    }

    ZeroMemory(bundle, sizeof(*bundle));

    safe_wcs_copy(base_path, PATH_BUFFER_COUNT, paths->locales_dir);
    PathAppendW(base_path, L"ko.json");
    base_json = read_text_file_utf8(base_path);
    if (base_json) {
        append_locale_entries_from_json(bundle, base_json);
        free(base_json);
    }

    if (code && code[0] != L'\0' && _wcsicmp(code, L"ko") != 0) {
        safe_wcs_copy(selected_path, PATH_BUFFER_COUNT, paths->locales_dir);
        PathAppendW(selected_path, code);
        wcscat_s(selected_path, PATH_BUFFER_COUNT, L".json");
        selected_json = read_text_file_utf8(selected_path);
        if (selected_json) {
            append_locale_entries_from_json(bundle, selected_json);
            free(selected_json);
        }
    }

    return bundle->entry_count > 0;
}

const wchar_t* locale_text(const LocaleBundle* bundle, const wchar_t* key)
{
    int index = 0;
    static wchar_t fallback[128];

    if (!bundle || !key) {
        return L"";
    }

    for (index = 0; index < bundle->entry_count; ++index) {
        if (wcscmp(bundle->entries[index].key, key) == 0) {
            return bundle->entries[index].value;
        }
    }

    safe_wcs_copy(fallback, 128, key);
    return fallback;
}
