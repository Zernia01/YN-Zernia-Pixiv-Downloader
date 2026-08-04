#include "config.h"

#include "json_utils.h"
#include "utf8.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

static void set_error(wchar_t* error_message, size_t error_message_count, const wchar_t* text)
{
    if (!error_message || error_message_count == 0) {
        return;
    }
    safe_wcs_copy(error_message, error_message_count, text);
}

static void load_utf8_string_field(const char* json, const char* key, wchar_t* output, size_t output_count)
{
    char buffer[1024];
    wchar_t* wide_value = NULL;

    if (!json_get_string(json, key, buffer, sizeof(buffer))) {
        return;
    }
    wide_value = utf8_to_wide_alloc(buffer);
    if (wide_value) {
        safe_wcs_copy(output, output_count, wide_value);
        free(wide_value);
    }
}

void config_set_defaults(AppConfig* config)
{
    if (!config) {
        return;
    }

    ZeroMemory(config, sizeof(*config));
    safe_wcs_copy(config->download_dir, PATH_BUFFER_COUNT, L"downloads");
    safe_wcs_copy(config->archive_file, PATH_BUFFER_COUNT, L"config/archive.json");
    safe_wcs_copy(config->sleep_request, 64, L"1.0-2.0");
    safe_wcs_copy(config->ugoira_format, 32, L"gif");
    config->auto_clipboard_to_targets = 0;
    config->allow_duplicate_downloads = 0;
    config->parallel_downloads = 3;
    config->profile_page_size = 32;
    config->download_illustrations = 1;
    config->download_manga = 0;
    config->download_novels = 0;
    config->auto_update = 1;
    safe_wcs_copy(config->github_repository, 2048, L"Zernia01/YN-Zernia-Pixiv-Downloader");
    config->write_metadata = 0;
    safe_wcs_copy(config->language, 32, L"ko");
    safe_wcs_copy(config->ffmpeg_path, PATH_BUFFER_COUNT, L"ffmpeg");
    safe_wcs_copy(config->artist_folder_format, 256, L"{user[name]} (@{user[account]}) [{user[id]}]");
    safe_wcs_copy(config->multi_image_folder_format, 256, L"{title} [{id}]");
    safe_wcs_copy(config->single_image_filename_format, 256, L"{title} ({id}).{extension}");
    safe_wcs_copy(config->multi_image_filename_format, 256, L"{id}_{num:>02}.{extension}");
}

int config_load(const AppPaths* paths, AppConfig* config, wchar_t* error_message, size_t error_message_count)
{
    char* json = NULL;

    if (!paths || !config) {
        set_error(error_message, error_message_count, L"Invalid config load arguments.");
        return 0;
    }

    config_set_defaults(config);
    json = read_text_file_utf8(paths->config_path);
    if (!json) {
        config_save(paths, config, error_message, error_message_count);
        return 1;
    }

    load_utf8_string_field(json, "refresh_token", config->refresh_token, 512);
    load_utf8_string_field(json, "download_dir", config->download_dir, PATH_BUFFER_COUNT);
    load_utf8_string_field(json, "archive_file", config->archive_file, PATH_BUFFER_COUNT);
    load_utf8_string_field(json, "sleep_request", config->sleep_request, 64);
    load_utf8_string_field(json, "ugoira_format", config->ugoira_format, 32);
    load_utf8_string_field(json, "github_repository", config->github_repository, 2048);
    if (!config->github_repository[0]) {
        load_utf8_string_field(json, "update_manifest_url", config->github_repository, 2048);
        if (!wcsstr(config->github_repository, L"github.com/")) {
            config->github_repository[0] = L'\0';
        }
    }
    load_utf8_string_field(json, "language", config->language, 32);
    load_utf8_string_field(json, "ffmpeg_path", config->ffmpeg_path, PATH_BUFFER_COUNT);
    load_utf8_string_field(json, "artist_folder_format", config->artist_folder_format, 256);
    load_utf8_string_field(json, "multi_image_folder_format", config->multi_image_folder_format, 256);
    load_utf8_string_field(json, "single_image_filename_format", config->single_image_filename_format, 256);
    load_utf8_string_field(json, "multi_image_filename_format", config->multi_image_filename_format, 256);
    config->write_metadata = json_get_bool(json, "write_metadata", config->write_metadata);
    config->auto_clipboard_to_targets = json_get_bool(json, "auto_clipboard_to_targets", config->auto_clipboard_to_targets);
    config->allow_duplicate_downloads = json_get_bool(json, "allow_duplicate_downloads", config->allow_duplicate_downloads);
    config->download_illustrations = json_get_bool(json, "download_illustrations", config->download_illustrations);
    config->download_manga = json_get_bool(json, "download_manga", config->download_manga);
    config->download_novels = json_get_bool(json, "download_novels", config->download_novels);
    config->auto_update = json_get_bool(json, "auto_update", config->auto_update);
    config->parallel_downloads = json_get_int(json, "parallel_downloads", config->parallel_downloads);
    if (config->parallel_downloads < 2) {
        config->parallel_downloads = 2;
    } else if (config->parallel_downloads > 5) {
        config->parallel_downloads = 5;
    }
    config->profile_page_size = json_get_int(json, "profile_page_size", config->profile_page_size);
    if (config->profile_page_size < 1) {
        config->profile_page_size = 32;
    }

    free(json);
    set_error(error_message, error_message_count, L"");
    return 1;
}

int config_save(const AppPaths* paths, const AppConfig* config, wchar_t* error_message, size_t error_message_count)
{
    char* refresh_token = NULL;
    char* download_dir = NULL;
    char* archive_file = NULL;
    char* sleep_request = NULL;
    char* ugoira_format = NULL;
    char* language = NULL;
    char* ffmpeg_path = NULL;
    char* artist_folder_format = NULL;
    char* multi_image_folder_format = NULL;
    char* single_image_filename_format = NULL;
    char* multi_image_filename_format = NULL;
    char* github_repository = NULL;
    char e1[2048], e2[2048], e3[2048], e4[512], e5[256], e6[256], e7[2048], e8[1024], e9[1024], e10[1024], e11[1024], e12[4096];
    char buffer[24576];

    if (!paths || !config) {
        set_error(error_message, error_message_count, L"Invalid config save arguments.");
        return 0;
    }

    refresh_token = wide_to_utf8_alloc(config->refresh_token);
    download_dir = wide_to_utf8_alloc(config->download_dir);
    archive_file = wide_to_utf8_alloc(config->archive_file);
    sleep_request = wide_to_utf8_alloc(config->sleep_request);
    ugoira_format = wide_to_utf8_alloc(config->ugoira_format);
    language = wide_to_utf8_alloc(config->language);
    ffmpeg_path = wide_to_utf8_alloc(config->ffmpeg_path);
    artist_folder_format = wide_to_utf8_alloc(config->artist_folder_format);
    multi_image_folder_format = wide_to_utf8_alloc(config->multi_image_folder_format);
    single_image_filename_format = wide_to_utf8_alloc(config->single_image_filename_format);
    multi_image_filename_format = wide_to_utf8_alloc(config->multi_image_filename_format);
    github_repository = wide_to_utf8_alloc(config->github_repository);

    json_escape_string(refresh_token ? refresh_token : "", e1, sizeof(e1));
    json_escape_string(download_dir ? download_dir : "", e2, sizeof(e2));
    json_escape_string(archive_file ? archive_file : "", e3, sizeof(e3));
    json_escape_string(sleep_request ? sleep_request : "", e4, sizeof(e4));
    json_escape_string(ugoira_format ? ugoira_format : "", e5, sizeof(e5));
    json_escape_string(language ? language : "", e6, sizeof(e6));
    json_escape_string(ffmpeg_path ? ffmpeg_path : "", e7, sizeof(e7));
    json_escape_string(artist_folder_format ? artist_folder_format : "", e8, sizeof(e8));
    json_escape_string(multi_image_folder_format ? multi_image_folder_format : "", e9, sizeof(e9));
    json_escape_string(single_image_filename_format ? single_image_filename_format : "", e10, sizeof(e10));
    json_escape_string(multi_image_filename_format ? multi_image_filename_format : "", e11, sizeof(e11));
    json_escape_string(github_repository ? github_repository : "", e12, sizeof(e12));

    snprintf(
        buffer,
        sizeof(buffer),
        "{\n"
        "  \"refresh_token\": \"%s\",\n"
        "  \"download_dir\": \"%s\",\n"
        "  \"archive_file\": \"%s\",\n"
        "  \"sleep_request\": \"%s\",\n"
        "  \"write_metadata\": %s,\n"
        "  \"ugoira_format\": \"%s\",\n"
        "  \"auto_clipboard_to_targets\": %s,\n"
        "  \"allow_duplicate_downloads\": %s,\n"
        "  \"parallel_downloads\": %d,\n"
        "  \"profile_page_size\": %d,\n"
        "  \"download_illustrations\": %s,\n"
        "  \"download_manga\": %s,\n"
        "  \"download_novels\": %s,\n"
        "  \"auto_update\": %s,\n"
        "  \"github_repository\": \"%s\",\n"
        "  \"language\": \"%s\",\n"
        "  \"ffmpeg_path\": \"%s\",\n"
        "  \"artist_folder_format\": \"%s\",\n"
        "  \"multi_image_folder_format\": \"%s\",\n"
        "  \"single_image_filename_format\": \"%s\",\n"
        "  \"multi_image_filename_format\": \"%s\"\n"
        "}\n",
        e1, e2, e3, e4,
        config->write_metadata ? "true" : "false",
        e5,
        config->auto_clipboard_to_targets ? "true" : "false",
        config->allow_duplicate_downloads ? "true" : "false",
        config->parallel_downloads,
        config->profile_page_size,
        config->download_illustrations ? "true" : "false",
        config->download_manga ? "true" : "false",
        config->download_novels ? "true" : "false",
        config->auto_update ? "true" : "false",
        e12,
        e6, e7, e8, e9, e10, e11
    );

    free(refresh_token);
    free(download_dir);
    free(archive_file);
    free(sleep_request);
    free(ugoira_format);
    free(language);
    free(ffmpeg_path);
    free(artist_folder_format);
    free(multi_image_folder_format);
    free(single_image_filename_format);
    free(multi_image_filename_format);
    free(github_repository);

    {
        wchar_t config_directory[PATH_BUFFER_COUNT];
        safe_wcs_copy(config_directory, PATH_BUFFER_COUNT, paths->config_path);
        PathRemoveFileSpecW(config_directory);
        SHCreateDirectoryExW(NULL, config_directory, NULL);
    }

    if (!write_text_file_utf8(paths->config_path, buffer)) {
        set_error(error_message, error_message_count, L"Could not save config.json.");
        return 0;
    }

    set_error(error_message, error_message_count, L"");
    return 1;
}
