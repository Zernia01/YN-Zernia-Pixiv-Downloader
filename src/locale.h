#pragma once

#include "app_paths.h"

#define MAX_LANGUAGE_ITEMS 32
#define MAX_LOCALE_PAIRS 256

typedef struct LanguageItem {
    wchar_t code[32];
    wchar_t display_name[64];
} LanguageItem;

typedef struct LocaleEntry {
    wchar_t key[128];
    wchar_t value[512];
} LocaleEntry;

typedef struct LocaleBundle {
    LocaleEntry entries[MAX_LOCALE_PAIRS];
    int entry_count;
} LocaleBundle;

int locale_list_languages(const AppPaths* paths, LanguageItem* items, int max_items);
int locale_load_bundle(const AppPaths* paths, const wchar_t* code, LocaleBundle* bundle);
const wchar_t* locale_text(const LocaleBundle* bundle, const wchar_t* key);
