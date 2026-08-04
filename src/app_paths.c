#include "app_paths.h"

#include "utf8.h"

#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "shlwapi.lib")

static int is_app_root_directory(const wchar_t* directory);

static int directory_exists(const wchar_t* path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int file_exists(const wchar_t* path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void join_path(const wchar_t* left, const wchar_t* right, wchar_t* output, size_t output_count)
{
    safe_wcs_copy(output, output_count, left);
    PathAppendW(output, right);
}

static int try_find_app_root_in_children(const wchar_t* directory, wchar_t* output, size_t output_count)
{
    wchar_t search_pattern[PATH_BUFFER_COUNT];
    wchar_t child_path[PATH_BUFFER_COUNT];
    WIN32_FIND_DATAW find_data;
    HANDLE handle = INVALID_HANDLE_VALUE;

    safe_wcs_copy(search_pattern, PATH_BUFFER_COUNT, directory);
    PathAppendW(search_pattern, L"*");

    handle = FindFirstFileW(search_pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }

        safe_wcs_copy(child_path, PATH_BUFFER_COUNT, directory);
        PathAppendW(child_path, find_data.cFileName);
        if (is_app_root_directory(child_path)) {
            safe_wcs_copy(output, output_count, child_path);
            FindClose(handle);
            return 1;
        }
    } while (FindNextFileW(handle, &find_data));

    FindClose(handle);
    return 0;
}

static int is_app_root_directory(const wchar_t* directory)
{
    wchar_t config_candidate[PATH_BUFFER_COUNT];
    wchar_t example_candidate[PATH_BUFFER_COUNT];
    wchar_t locales_candidate[PATH_BUFFER_COUNT];

    if (!directory || !directory[0]) {
        return 0;
    }

    join_path(directory, L"config\\config.json", config_candidate, PATH_BUFFER_COUNT);
    join_path(directory, L"config\\config.example.json", example_candidate, PATH_BUFFER_COUNT);
    join_path(directory, L"locales\\ko.json", locales_candidate, PATH_BUFFER_COUNT);

    return (file_exists(config_candidate) || file_exists(example_candidate)) && file_exists(locales_candidate);
}

static int try_find_app_root_from(const wchar_t* start_dir, wchar_t* output, size_t output_count)
{
    wchar_t current[PATH_BUFFER_COUNT];
    int depth = 0;

    safe_wcs_copy(current, PATH_BUFFER_COUNT, start_dir);
    for (depth = 0; depth < 12; ++depth) {
        if (is_app_root_directory(current)) {
            safe_wcs_copy(output, output_count, current);
            return 1;
        }

        if (try_find_app_root_in_children(current, output, output_count)) {
            return 1;
        }

        if (!PathRemoveFileSpecW(current)) {
            break;
        }
    }
    return 0;
}

int discover_app_paths(AppPaths* paths)
{
    wchar_t module_path[PATH_BUFFER_COUNT];
    wchar_t current_directory[PATH_BUFFER_COUNT];
    wchar_t start_dir[PATH_BUFFER_COUNT];
    wchar_t app_root[PATH_BUFFER_COUNT];

    if (!paths) {
        return 0;
    }

    ZeroMemory(paths, sizeof(*paths));
    ZeroMemory(module_path, sizeof(module_path));
    ZeroMemory(current_directory, sizeof(current_directory));
    ZeroMemory(start_dir, sizeof(start_dir));
    ZeroMemory(app_root, sizeof(app_root));

    GetModuleFileNameW(NULL, module_path, PATH_BUFFER_COUNT);
    safe_wcs_copy(start_dir, PATH_BUFFER_COUNT, module_path);
    PathRemoveFileSpecW(start_dir);

    GetCurrentDirectoryW(PATH_BUFFER_COUNT, current_directory);

    if (!try_find_app_root_from(start_dir, app_root, PATH_BUFFER_COUNT)
        && !try_find_app_root_from(current_directory, app_root, PATH_BUFFER_COUNT)) {
        safe_wcs_copy(app_root, PATH_BUFFER_COUNT, start_dir);
    }

    safe_wcs_copy(paths->app_root, PATH_BUFFER_COUNT, app_root);
    safe_wcs_copy(paths->workspace_root, PATH_BUFFER_COUNT, app_root);
    PathRemoveFileSpecW(paths->workspace_root);

    join_path(paths->app_root, L"data", paths->data_dir, PATH_BUFFER_COUNT);
    join_path(paths->app_root, L"config\\config.json", paths->config_path, PATH_BUFFER_COUNT);
    join_path(paths->app_root, L"locales", paths->locales_dir, PATH_BUFFER_COUNT);
    join_path(paths->app_root, L"README.md", paths->readme_path, PATH_BUFFER_COUNT);
    join_path(paths->app_root, L"config\\list.json", paths->targets_path, PATH_BUFFER_COUNT);
    join_path(paths->data_dir, L"pixiv_down.ico", paths->icon_ico_path, PATH_BUFFER_COUNT);
    join_path(paths->data_dir, L"pixiv_down.png", paths->icon_png_path, PATH_BUFFER_COUNT);
    join_path(paths->data_dir, L"siren.mp3", paths->siren_mp3_path, PATH_BUFFER_COUNT);

    return directory_exists(paths->app_root);
}

void resolve_against_app_root(const AppPaths* paths, const wchar_t* value, wchar_t* output, size_t output_count)
{
    if (!paths || !output || output_count == 0) {
        return;
    }

    if (!value || value[0] == L'\0') {
        safe_wcs_copy(output, output_count, paths->app_root);
        return;
    }

    if (PathIsRelativeW(value)) {
        safe_wcs_copy(output, output_count, paths->app_root);
        PathAppendW(output, value);
        return;
    }

    safe_wcs_copy(output, output_count, value);
}

int detect_ffmpeg_path(const AppPaths* paths, const wchar_t* configured_value, wchar_t* output, size_t output_count)
{
    wchar_t candidate[PATH_BUFFER_COUNT];

    if (!output || output_count == 0) {
        return 0;
    }
    output[0] = L'\0';

    if (configured_value && configured_value[0] != L'\0') {
        if (!wcschr(configured_value, L'\\') && !wcschr(configured_value, L'/') && !wcschr(configured_value, L':')) {
            DWORD result = SearchPathW(NULL, configured_value, NULL, (DWORD)output_count, output, NULL);
            if (result > 0 && result < output_count) {
                return 1;
            }
        }

        resolve_against_app_root(paths, configured_value, candidate, PATH_BUFFER_COUNT);
        if (file_exists(candidate)) {
            safe_wcs_copy(output, output_count, candidate);
            return 1;
        }

        if (directory_exists(candidate)) {
            wchar_t exe_path[PATH_BUFFER_COUNT];
            join_path(candidate, L"ffmpeg.exe", exe_path, PATH_BUFFER_COUNT);
            if (file_exists(exe_path)) {
                safe_wcs_copy(output, output_count, exe_path);
                return 1;
            }

            join_path(candidate, L"bin\\ffmpeg.exe", exe_path, PATH_BUFFER_COUNT);
            if (file_exists(exe_path)) {
                safe_wcs_copy(output, output_count, exe_path);
                return 1;
            }
        }
    }

    if (SearchPathW(NULL, L"ffmpeg.exe", NULL, (DWORD)output_count, output, NULL) > 0) {
        return 1;
    }

    output[0] = L'\0';
    return 0;
}
