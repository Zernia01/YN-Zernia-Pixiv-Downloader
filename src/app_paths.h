#pragma once

#include <windows.h>

#define PATH_BUFFER_COUNT 1024

typedef struct AppPaths {
    wchar_t workspace_root[PATH_BUFFER_COUNT];
    wchar_t app_root[PATH_BUFFER_COUNT];
    wchar_t data_dir[PATH_BUFFER_COUNT];
    wchar_t config_path[PATH_BUFFER_COUNT];
    wchar_t locales_dir[PATH_BUFFER_COUNT];
    wchar_t readme_path[PATH_BUFFER_COUNT];
    wchar_t targets_path[PATH_BUFFER_COUNT];
    wchar_t icon_ico_path[PATH_BUFFER_COUNT];
    wchar_t icon_png_path[PATH_BUFFER_COUNT];
    wchar_t siren_mp3_path[PATH_BUFFER_COUNT];
} AppPaths;

int discover_app_paths(AppPaths* paths);
void resolve_against_app_root(const AppPaths* paths, const wchar_t* value, wchar_t* output, size_t output_count);
int detect_ffmpeg_path(const AppPaths* paths, const wchar_t* configured_value, wchar_t* output, size_t output_count);
