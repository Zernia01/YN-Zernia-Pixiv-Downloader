#pragma once

#include "app_paths.h"

typedef struct AppConfig {
    wchar_t refresh_token[512];
    wchar_t download_dir[PATH_BUFFER_COUNT];
    wchar_t archive_file[PATH_BUFFER_COUNT];
    wchar_t sleep_request[64];
    int write_metadata;
    wchar_t ugoira_format[32];
    int auto_clipboard_to_targets;
    int allow_duplicate_downloads;
    int parallel_downloads;
    int profile_page_size;
    int download_illustrations;
    int download_manga;
    int download_novels;
    int auto_update;
    wchar_t github_repository[2048];
    wchar_t language[32];
    wchar_t ffmpeg_path[PATH_BUFFER_COUNT];
    wchar_t artist_folder_format[256];
    wchar_t multi_image_folder_format[256];
    wchar_t single_image_filename_format[256];
    wchar_t multi_image_filename_format[256];
} AppConfig;

void config_set_defaults(AppConfig* config);
int config_load(const AppPaths* paths, AppConfig* config, wchar_t* error_message, size_t error_message_count);
int config_save(const AppPaths* paths, const AppConfig* config, wchar_t* error_message, size_t error_message_count);
