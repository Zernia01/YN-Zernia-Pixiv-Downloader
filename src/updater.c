#include "updater.h"

#include "http_client.h"
#include "json_utils.h"
#include "utf8.h"

#include <bcrypt.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "bcrypt.lib")

static void set_error(wchar_t* output, size_t output_count, const wchar_t* text)
{
    if (!output || output_count == 0) {
        return;
    }
    wcsncpy_s(output, output_count, text ? text : L"", _TRUNCATE);
}

static int is_https_url(const wchar_t* url)
{
    return url && _wcsnicmp(url, L"https://", 8) == 0;
}

static int compare_versions(const wchar_t* left, const wchar_t* right)
{
    const wchar_t* l = left;
    const wchar_t* r = right;

    while ((l && *l) || (r && *r)) {
        unsigned long left_part = 0;
        unsigned long right_part = 0;

        while (l && *l && (*l < L'0' || *l > L'9')) {
            ++l;
        }
        while (r && *r && (*r < L'0' || *r > L'9')) {
            ++r;
        }
        while (l && *l >= L'0' && *l <= L'9') {
            left_part = left_part * 10 + (unsigned long)(*l - L'0');
            ++l;
        }
        while (r && *r >= L'0' && *r <= L'9') {
            right_part = right_part * 10 + (unsigned long)(*r - L'0');
            ++r;
        }
        if (left_part > right_part) {
            return 1;
        }
        if (left_part < right_part) {
            return -1;
        }
        if ((!l || !*l) && (!r || !*r)) {
            break;
        }
    }
    return 0;
}

static int is_github_name_character(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z')
        || (ch >= L'A' && ch <= L'Z')
        || (ch >= L'0' && ch <= L'9')
        || ch == L'-'
        || ch == L'_'
        || ch == L'.';
}

static int build_github_latest_release_url(
    const wchar_t* repository,
    wchar_t* api_url,
    size_t api_url_count
)
{
    const wchar_t* p = repository;
    wchar_t owner[128];
    wchar_t name[128];
    size_t owner_length = 0;
    size_t name_length = 0;

    if (!repository || !api_url || api_url_count == 0) {
        return 0;
    }
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    if (_wcsnicmp(p, L"https://github.com/", 19) == 0) {
        p += 19;
    } else if (_wcsnicmp(p, L"github.com/", 11) == 0) {
        p += 11;
    } else if (_wcsnicmp(p, L"https://api.github.com/repos/", 29) == 0) {
        p += 29;
    } else if (wcsstr(p, L"://")) {
        return 0;
    }

    while (is_github_name_character(*p) && owner_length + 1 < _countof(owner)) {
        owner[owner_length++] = *p++;
    }
    owner[owner_length] = L'\0';
    if (owner_length == 0 || *p != L'/') {
        return 0;
    }
    ++p;
    while (is_github_name_character(*p) && name_length + 1 < _countof(name)) {
        name[name_length++] = *p++;
    }
    name[name_length] = L'\0';
    if (name_length > 4 && _wcsicmp(name + name_length - 4, L".git") == 0) {
        name[name_length - 4] = L'\0';
        name_length -= 4;
    }
    if (name_length == 0) {
        return 0;
    }

    return swprintf_s(
        api_url,
        api_url_count,
        L"https://api.github.com/repos/%ls/%ls/releases/latest",
        owner,
        name
    ) > 0;
}

static const char* skip_ascii_ws(const char* p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static const char* skip_json_string_local(const char* p)
{
    if (!p || *p != '"') {
        return p;
    }
    ++p;
    while (*p) {
        if (*p == '\\' && p[1]) {
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

static const char* skip_json_object_local(const char* p)
{
    int depth = 0;

    if (!p || *p != '{') {
        return p;
    }
    while (*p) {
        if (*p == '"') {
            p = skip_json_string_local(p);
            continue;
        }
        if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            --depth;
            if (depth == 0) {
                return p + 1;
            }
        }
        ++p;
    }
    return p;
}

static int ascii_contains_case_insensitive(const char* text, const char* needle)
{
    size_t needle_length = 0;
    const char* p = text;

    if (!text || !needle || !needle[0]) {
        return 0;
    }
    needle_length = strlen(needle);
    while (*p) {
        if (_strnicmp(p, needle, needle_length) == 0) {
            return 1;
        }
        ++p;
    }
    return 0;
}

static int has_exe_extension(const char* name)
{
    size_t length = name ? strlen(name) : 0;
    return length > 4 && _stricmp(name + length - 4, ".exe") == 0;
}

static int is_sha256_hex(const char* text)
{
    int index = 0;

    if (!text || strlen(text) != 64) {
        return 0;
    }
    for (index = 0; index < 64; ++index) {
        if (!isxdigit((unsigned char)text[index])) {
            return 0;
        }
    }
    return 1;
}

static int select_github_release_executable(
    const char* release_json,
    char* download_url,
    size_t download_url_count,
    char* sha256,
    size_t sha256_count
)
{
    char* assets = NULL;
    const char* p = NULL;
    int best_score = -1;

    assets = json_extract_array_alloc(release_json, "assets");
    if (!assets) {
        return 0;
    }
    p = skip_ascii_ws(assets);
    if (*p == '[') {
        ++p;
    }

    while (*p) {
        const char* object_end = NULL;
        size_t object_length = 0;
        char* object_json = NULL;
        char name[512];
        char url[4096];
        char digest[128];
        int score = 0;

        p = skip_ascii_ws(p);
        if (*p == ']') {
            break;
        }
        if (*p != '{') {
            ++p;
            continue;
        }
        object_end = skip_json_object_local(p);
        if (!object_end || object_end <= p || object_end[-1] != '}') {
            break;
        }
        object_length = (size_t)(object_end - p);
        object_json = (char*)calloc(object_length + 1, sizeof(char));
        if (!object_json) {
            break;
        }
        memcpy(object_json, p, object_length);
        object_json[object_length] = '\0';

        name[0] = '\0';
        url[0] = '\0';
        digest[0] = '\0';
        json_get_string(object_json, "name", name, sizeof(name));
        json_get_string(object_json, "browser_download_url", url, sizeof(url));
        json_get_string(object_json, "digest", digest, sizeof(digest));
        free(object_json);

        if (has_exe_extension(name)
            && _strnicmp(digest, "sha256:", 7) == 0
            && is_sha256_hex(digest + 7)
            && _strnicmp(url, "https://github.com/", 19) == 0) {
            score = 1;
            if (ascii_contains_case_insensitive(name, "zernia")) {
                score += 20;
            }
            if (ascii_contains_case_insensitive(name, "pixiv")) {
                score += 10;
            }
            if (score > best_score) {
                strcpy_s(download_url, download_url_count, url);
                strcpy_s(sha256, sha256_count, digest + 7);
                best_score = score;
            }
        }
        p = object_end;
        p = skip_ascii_ws(p);
        if (*p == ',') {
            ++p;
        }
    }

    free(assets);
    return best_score >= 0;
}

static int calculate_file_sha256(const wchar_t* path, wchar_t output[65])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR hash_object = NULL;
    DWORD hash_object_size = 0;
    DWORD hash_size = 0;
    DWORD result_size = 0;
    BYTE digest[32];
    BYTE buffer[65536];
    FILE* file = NULL;
    size_t bytes_read = 0;
    int success = 0;
    int index = 0;

    if (!path || !output) {
        return 0;
    }
    output[0] = L'\0';
    if (_wfopen_s(&file, path, L"rb") != 0 || !file) {
        return 0;
    }
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0
        || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hash_object_size, sizeof(hash_object_size), &result_size, 0) < 0
        || BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_size, sizeof(hash_size), &result_size, 0) < 0
        || hash_size != sizeof(digest)) {
        goto cleanup;
    }
    hash_object = (PUCHAR)calloc(hash_object_size, 1);
    if (!hash_object
        || BCryptCreateHash(algorithm, &hash, hash_object, hash_object_size, NULL, 0, 0) < 0) {
        goto cleanup;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (BCryptHashData(hash, buffer, (ULONG)bytes_read, 0) < 0) {
            goto cleanup;
        }
    }
    if (ferror(file) || BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) {
        goto cleanup;
    }
    for (index = 0; index < 32; ++index) {
        swprintf_s(output + index * 2, 65 - (size_t)index * 2, L"%02x", digest[index]);
    }
    success = 1;

cleanup:
    if (file) {
        fclose(file);
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    free(hash_object);
    return success;
}

int updater_check_github_release(
    const wchar_t* repository,
    const wchar_t* current_version,
    UpdateManifest* manifest_out,
    int* update_available_out,
    wchar_t* error_message,
    size_t error_message_count
)
{
    HttpResponse response;
    wchar_t request_error[512];
    wchar_t api_url[2048];
    char version_utf8[128];
    char download_url_utf8[4096];
    char sha256_utf8[128];
    wchar_t* version_wide = NULL;
    wchar_t* download_url_wide = NULL;
    wchar_t* sha256_wide = NULL;

    if (manifest_out) {
        ZeroMemory(manifest_out, sizeof(*manifest_out));
    }
    if (update_available_out) {
        *update_available_out = 0;
    }
    if (!manifest_out || !current_version
        || !build_github_latest_release_url(repository, api_url, _countof(api_url))) {
        set_error(error_message, error_message_count, L"Enter a GitHub repository as OWNER/REPOSITORY or an https://github.com/OWNER/REPOSITORY URL.");
        return 0;
    }

    ZeroMemory(&response, sizeof(response));
    if (!http_request_utf8(
            L"GET",
            api_url,
            L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
            NULL,
            0,
            &response,
            request_error,
            _countof(request_error))) {
        set_error(error_message, error_message_count, request_error[0] ? request_error : L"Could not contact GitHub Releases.");
        return 0;
    }
    if (response.status_code < 200 || response.status_code >= 300 || !response.body) {
        DWORD status_code = response.status_code;
        http_response_free(&response);
        if (status_code == 404) {
            set_error(error_message, error_message_count, L"GitHub has no published latest release for this repository, or the repository is private.");
        } else if (status_code == 403) {
            set_error(error_message, error_message_count, L"GitHub API access was denied or the anonymous API rate limit was reached.");
        } else {
            set_error(error_message, error_message_count, L"GitHub Releases returned an HTTP error.");
        }
        return 0;
    }

    version_utf8[0] = '\0';
    download_url_utf8[0] = '\0';
    sha256_utf8[0] = '\0';
    json_get_string(response.body, "tag_name", version_utf8, sizeof(version_utf8));
    version_wide = utf8_to_wide_alloc(version_utf8);
    if (!version_wide || !version_wide[0]) {
        http_response_free(&response);
        free(version_wide);
        set_error(error_message, error_message_count, L"The latest GitHub Release needs a valid version tag.");
        return 0;
    }

    wcsncpy_s(manifest_out->version, _countof(manifest_out->version), version_wide, _TRUNCATE);
    if (compare_versions(manifest_out->version, current_version) <= 0) {
        http_response_free(&response);
        free(version_wide);
        set_error(error_message, error_message_count, L"");
        return 1;
    }

    select_github_release_executable(
        response.body,
        download_url_utf8,
        sizeof(download_url_utf8),
        sha256_utf8,
        sizeof(sha256_utf8)
    );
    http_response_free(&response);

    download_url_wide = utf8_to_wide_alloc(download_url_utf8);
    sha256_wide = utf8_to_wide_alloc(sha256_utf8);
    if (!download_url_wide || !is_https_url(download_url_wide)
        || !sha256_wide || wcslen(sha256_wide) != 64) {
        free(version_wide);
        free(download_url_wide);
        free(sha256_wide);
        set_error(error_message, error_message_count, L"The latest GitHub Release needs a version tag and an EXE asset with GitHub SHA-256 digest metadata.");
        return 0;
    }

    wcsncpy_s(manifest_out->download_url, _countof(manifest_out->download_url), download_url_wide, _TRUNCATE);
    wcsncpy_s(manifest_out->sha256, _countof(manifest_out->sha256), sha256_wide, _TRUNCATE);
    if (update_available_out) {
        *update_available_out = 1;
    }
    free(version_wide);
    free(download_url_wide);
    free(sha256_wide);
    set_error(error_message, error_message_count, L"");
    return 1;
}

int updater_download_and_verify(
    const UpdateManifest* manifest,
    wchar_t* staged_path,
    size_t staged_path_count,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t temporary_directory[MAX_PATH];
    wchar_t actual_hash[65];
    wchar_t download_error[512];

    if (!manifest || !staged_path || staged_path_count == 0 || !is_https_url(manifest->download_url)) {
        set_error(error_message, error_message_count, L"Invalid update download arguments.");
        return 0;
    }
    if (!GetTempPathW(_countof(temporary_directory), temporary_directory)) {
        set_error(error_message, error_message_count, L"Could not resolve the temporary update directory.");
        return 0;
    }
    swprintf_s(
        staged_path,
        staged_path_count,
        L"%lsYN_Zernia_Pixiv_Update_%lu.exe",
        temporary_directory,
        (unsigned long)GetCurrentProcessId()
    );

    if (!http_download_to_file(manifest->download_url, NULL, staged_path, download_error, _countof(download_error))) {
        set_error(error_message, error_message_count, download_error[0] ? download_error : L"Could not download the update executable.");
        return 0;
    }
    if (!calculate_file_sha256(staged_path, actual_hash)
        || _wcsicmp(actual_hash, manifest->sha256) != 0) {
        DeleteFileW(staged_path);
        set_error(error_message, error_message_count, L"The downloaded update failed SHA-256 verification.");
        return 0;
    }
    set_error(error_message, error_message_count, L"");
    return 1;
}

int updater_launch_apply(
    const wchar_t* staged_path,
    DWORD parent_process_id,
    const wchar_t* target_path,
    wchar_t* error_message,
    size_t error_message_count
)
{
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    wchar_t command_line[4096];

    if (!staged_path || !target_path) {
        set_error(error_message, error_message_count, L"Invalid update launch arguments.");
        return 0;
    }
    swprintf_s(
        command_line,
        _countof(command_line),
        L"\"%ls\" --apply-update %lu \"%ls\"",
        staged_path,
        (unsigned long)parent_process_id,
        target_path
    );
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(staged_path, command_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        set_error(error_message, error_message_count, L"Could not launch the update helper.");
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    set_error(error_message, error_message_count, L"");
    return 1;
}

int updater_apply_staged_update(DWORD parent_process_id, const wchar_t* target_path)
{
    HANDLE parent_process = NULL;
    wchar_t staged_path[MAX_PATH];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    int attempt = 0;

    if (!target_path || !target_path[0] || !GetModuleFileNameW(NULL, staged_path, _countof(staged_path))) {
        return 0;
    }
    parent_process = OpenProcess(SYNCHRONIZE, FALSE, parent_process_id);
    if (parent_process) {
        WaitForSingleObject(parent_process, INFINITE);
        CloseHandle(parent_process);
    }

    for (attempt = 0; attempt < 40; ++attempt) {
        if (CopyFileW(staged_path, target_path, FALSE)) {
            break;
        }
        Sleep(250);
    }
    if (attempt >= 40) {
        return 0;
    }

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(target_path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}
