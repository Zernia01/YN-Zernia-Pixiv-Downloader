#include "pixiv_api.h"

#include "http_client.h"
#include "json_utils.h"
#include "utf8.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

#define PIXIV_CLIENT_ID "MOBrBDS8blbauoSck0ZfDbtuzpyT"
#define PIXIV_CLIENT_SECRET "lsACyCD94FhDUtGTXi3QzcFE2uU1hqtDaKeqrdwj"
#define PIXIV_HASH_SECRET "28c1fdd170a5204386cb1313c7077b34f83e4aaf4aa829ce78c231e05b0bae2c"
#define PIXIV_DEFAULT_PROFILE_PAGE_SIZE 32
#define PIXIV_SENSITIVE_FILTER_MODE L"all"
#define PIXIV_AUTH_CALLBACK_URL "https://app-api.pixiv.net/web/v1/users/auth/pixiv/callback"
#define PIXIV_AUTH_LOGIN_URL "https://app-api.pixiv.net/web/v1/login"
#define PIXIV_DIAGNOSTIC_BUFFER_COUNT 4096
#define PIXIV_HTTP_RETRY_COUNT 5
#define PIXIV_FILE_RETRY_COUNT 4

typedef enum PixivProfilePageTargetType {
    PIXIV_PROFILE_PAGE_TARGET_NONE = 0,
    PIXIV_PROFILE_PAGE_TARGET_ARTWORKS = 1,
    PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS = 2,
    PIXIV_PROFILE_PAGE_TARGET_MANGA = 3,
    PIXIV_PROFILE_PAGE_TARGET_NOVELS = 4
} PixivProfilePageTargetType;

typedef enum PixivDownloadOutcome {
    PIXIV_DOWNLOAD_OUTCOME_FAILED = 0,
    PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED = 1,
    PIXIV_DOWNLOAD_OUTCOME_SKIPPED = 2
} PixivDownloadOutcome;

typedef struct PixivUgoiraFrame {
    wchar_t file[128];
    int delay;
} PixivUgoiraFrame;

typedef struct PixivUgoiraInfo {
    wchar_t zip_url[1024];
    int frame_count;
    PixivUgoiraFrame* frames;
} PixivUgoiraInfo;

typedef struct PixivUserArtworkEntry {
    int illust_id;
    char create_date[64];
} PixivUserArtworkEntry;

typedef struct PixivNovelInfo {
    int novel_id;
    int user_id;
    wchar_t title[256];
    wchar_t user_name[128];
    wchar_t user_account[128];
    wchar_t cover_url[1024];
    char* text_utf8;
    char* webview_json;
} PixivNovelInfo;

static int g_pixiv_detailed_logging_enabled = 0;
static __declspec(thread) wchar_t g_pixiv_last_diagnostic[PIXIV_DIAGNOSTIC_BUFFER_COUNT];
static SRWLOCK g_pixiv_archive_lock = SRWLOCK_INIT;

static void set_error(wchar_t* error_message, size_t error_message_count, const wchar_t* text)
{
    if (!error_message || error_message_count == 0) {
        return;
    }
    safe_wcs_copy(error_message, error_message_count, text);
}

static void sanitize_wide_text_for_log(wchar_t* text)
{
    wchar_t* cursor = text;
    if (!text) {
        return;
    }
    while (*cursor) {
        if (*cursor == L'\r' || *cursor == L'\n' || *cursor == L'\t') {
            *cursor = L' ';
        }
        ++cursor;
    }
}

static void trim_wide_trailing_spaces(wchar_t* text)
{
    size_t length = 0;
    if (!text) {
        return;
    }
    length = wcslen(text);
    while (length > 0 && (text[length - 1] == L' ' || text[length - 1] == L'\r' || text[length - 1] == L'\n' || text[length - 1] == L'\t')) {
        text[length - 1] = L'\0';
        --length;
    }
}

static void set_last_diagnostic_text(const wchar_t* text)
{
    if (!g_pixiv_detailed_logging_enabled) {
        return;
    }
    safe_wcs_copy(g_pixiv_last_diagnostic, PIXIV_DIAGNOSTIC_BUFFER_COUNT, text ? text : L"");
}

static void set_last_diagnosticf(const wchar_t* format, ...)
{
    va_list args;

    if (!g_pixiv_detailed_logging_enabled || !format) {
        return;
    }

    va_start(args, format);
    vswprintf_s(g_pixiv_last_diagnostic, _countof(g_pixiv_last_diagnostic), format, args);
    va_end(args);
}

static void set_nested_http_diagnostic(const wchar_t* context, const wchar_t* fallback)
{
    wchar_t http_detail[2048];

    if (!g_pixiv_detailed_logging_enabled) {
        return;
    }

    if (http_get_last_diagnostic(http_detail, _countof(http_detail)) && http_detail[0]) {
        set_last_diagnosticf(L"%ls | %ls", context ? context : L"Pixiv request failed.", http_detail);
        return;
    }

    if (fallback && fallback[0]) {
        set_last_diagnosticf(L"%ls | %ls", context ? context : L"Pixiv request failed.", fallback);
        return;
    }

    set_last_diagnostic_text(context ? context : L"Pixiv request failed.");
}

static void set_http_status_diagnostic(const wchar_t* context, const wchar_t* method, const wchar_t* url, const HttpResponse* response)
{
    wchar_t message[PIXIV_DIAGNOSTIC_BUFFER_COUNT];
    char snippet_utf8[256];
    wchar_t* snippet_wide = NULL;
    size_t snippet_length = 0;

    if (!g_pixiv_detailed_logging_enabled || !response) {
        return;
    }

    snippet_utf8[0] = '\0';
    if (response->body && response->body_size > 0) {
        snippet_length = response->body_size < sizeof(snippet_utf8) - 1 ? response->body_size : sizeof(snippet_utf8) - 1;
        memcpy(snippet_utf8, response->body, snippet_length);
        snippet_utf8[snippet_length] = '\0';
        utf8_trim_trailing_newlines(snippet_utf8);
        for (snippet_length = 0; snippet_utf8[snippet_length]; ++snippet_length) {
            if (snippet_utf8[snippet_length] == '\r' || snippet_utf8[snippet_length] == '\n' || snippet_utf8[snippet_length] == '\t') {
                snippet_utf8[snippet_length] = ' ';
            }
        }
        snippet_wide = utf8_to_wide_alloc(snippet_utf8);
        if (snippet_wide) {
            sanitize_wide_text_for_log(snippet_wide);
            trim_wide_trailing_spaces(snippet_wide);
        }
    }

    if (method && method[0] && url && url[0] && snippet_wide && snippet_wide[0]) {
        swprintf_s(
            message,
            _countof(message),
            L"%ls | %ls %ls | HTTP %lu | Response: %ls",
            context ? context : L"Pixiv request returned an HTTP error.",
            method,
            url,
            (unsigned long)response->status_code,
            snippet_wide
        );
    } else if (method && method[0] && url && url[0]) {
        swprintf_s(
            message,
            _countof(message),
            L"%ls | %ls %ls | HTTP %lu",
            context ? context : L"Pixiv request returned an HTTP error.",
            method,
            url,
            (unsigned long)response->status_code
        );
    } else if (snippet_wide && snippet_wide[0]) {
        swprintf_s(
            message,
            _countof(message),
            L"%ls | HTTP %lu | Response: %ls",
            context ? context : L"Pixiv request returned an HTTP error.",
            (unsigned long)response->status_code,
            snippet_wide
        );
    } else {
        swprintf_s(
            message,
            _countof(message),
            L"%ls | HTTP %lu",
            context ? context : L"Pixiv request returned an HTTP error.",
            (unsigned long)response->status_code
        );
    }

    free(snippet_wide);
    set_last_diagnostic_text(message);
}

void pixiv_set_detailed_logging(int enabled)
{
    g_pixiv_detailed_logging_enabled = enabled ? 1 : 0;
    http_set_detailed_errors_enabled(enabled);
    if (!g_pixiv_detailed_logging_enabled) {
        g_pixiv_last_diagnostic[0] = L'\0';
        http_clear_last_diagnostic();
    }
}

void pixiv_clear_last_diagnostic(void)
{
    g_pixiv_last_diagnostic[0] = L'\0';
    http_clear_last_diagnostic();
}

int pixiv_get_last_diagnostic(wchar_t* output, size_t output_count)
{
    if (!output || output_count == 0) {
        return 0;
    }
    output[0] = L'\0';
    if (g_pixiv_last_diagnostic[0]) {
        safe_wcs_copy(output, output_count, g_pixiv_last_diagnostic);
        return 1;
    }
    return http_get_last_diagnostic(output, output_count);
}

static int regular_file_exists(const wchar_t* path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void sleep_between_requests(const AppConfig* config)
{
    double min_seconds = 0.0;
    double max_seconds = 0.0;
    int parsed = 0;
    DWORD min_ms = 0;
    DWORD max_ms = 0;
    DWORD delay_ms = 0;

    if (!config || !config->sleep_request[0]) {
        return;
    }

    parsed = swscanf_s(config->sleep_request, L"%lf-%lf", &min_seconds, &max_seconds);
    if (parsed != 2) {
        parsed = swscanf_s(config->sleep_request, L"%lf", &min_seconds);
        if (parsed == 1) {
            max_seconds = min_seconds;
        }
    }
    if (parsed <= 0) {
        return;
    }

    if (min_seconds < 0.0) {
        min_seconds = 0.0;
    }
    if (max_seconds < 0.0) {
        max_seconds = 0.0;
    }
    if (max_seconds < min_seconds) {
        double temp = min_seconds;
        min_seconds = max_seconds;
        max_seconds = temp;
    }

    min_ms = (DWORD)(min_seconds * 1000.0 + 0.5);
    max_ms = (DWORD)(max_seconds * 1000.0 + 0.5);
    delay_ms = min_ms;
    if (max_ms > min_ms) {
        ULONG random_value = 0;
        if (BCryptGenRandom(NULL, (PUCHAR)&random_value, sizeof(random_value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
            random_value = GetTickCount() ^ GetCurrentThreadId();
        }
        delay_ms = min_ms + (DWORD)(random_value % (max_ms - min_ms + 1));
    }

    if (delay_ms > 0) {
        Sleep(delay_ms);
    }
}

void pixiv_sleep_between_requests(const AppConfig* config)
{
    sleep_between_requests(config);
}

static int pixiv_should_retry_http_status(DWORD status_code)
{
    return status_code == 408 || status_code == 425 || status_code == 429 || status_code >= 500;
}

static int pixiv_should_refresh_access_token_for_status(DWORD status_code)
{
    return status_code == 400 || status_code == 401 || status_code == 403;
}

static void pixiv_sleep_before_retry(const AppConfig* config, int attempt_index)
{
    sleep_between_requests(config);
    if (attempt_index > 0) {
        Sleep((DWORD)(350 * attempt_index));
    }
}

static void get_utc_time_string(char* output, size_t output_size)
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    snprintf(
        output,
        output_size,
        "%04u-%02u-%02uT%02u:%02u:%02u+00:00",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond
    );
}

static int compute_md5_hex(const char* text, char* output, size_t output_size)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD hash_object_size = 0;
    DWORD hash_length = 0;
    DWORD result_size = 0;
    PUCHAR hash_object = NULL;
    PUCHAR hash_bytes = NULL;
    NTSTATUS status;
    DWORD index = 0;

    if (!text || !output || output_size < 33) {
        return 0;
    }

    status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, NULL, 0);
    if (status < 0) {
        return 0;
    }

    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hash_object_size, sizeof(hash_object_size), &result_size, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }
    status = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_length, sizeof(hash_length), &result_size, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    hash_object = (PUCHAR)calloc(hash_object_size, sizeof(UCHAR));
    hash_bytes = (PUCHAR)calloc(hash_length, sizeof(UCHAR));
    if (!hash_object || !hash_bytes) {
        free(hash_object);
        free(hash_bytes);
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    status = BCryptCreateHash(alg, &hash, hash_object, hash_object_size, NULL, 0, 0);
    if (status >= 0) {
        status = BCryptHashData(hash, (PUCHAR)text, (ULONG)strlen(text), 0);
    }
    if (status >= 0) {
        status = BCryptFinishHash(hash, hash_bytes, hash_length, 0);
    }
    if (status < 0) {
        if (hash) {
            BCryptDestroyHash(hash);
        }
        free(hash_object);
        free(hash_bytes);
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    for (index = 0; index < hash_length && index * 2 + 1 < output_size; ++index) {
        sprintf_s(output + index * 2, output_size - index * 2, "%02x", hash_bytes[index]);
    }
    output[hash_length * 2] = '\0';

    BCryptDestroyHash(hash);
    free(hash_object);
    free(hash_bytes);
    BCryptCloseAlgorithmProvider(alg, 0);
    return 1;
}

static int compute_sha256_bytes(const unsigned char* input, size_t input_size, unsigned char* output, size_t output_size)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD hash_object_size = 0;
    DWORD hash_length = 0;
    DWORD result_size = 0;
    PUCHAR hash_object = NULL;
    NTSTATUS status;

    if (!input || !output || output_size < 32) {
        return 0;
    }

    status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) {
        return 0;
    }

    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hash_object_size, sizeof(hash_object_size), &result_size, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }
    status = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_length, sizeof(hash_length), &result_size, 0);
    if (status < 0 || hash_length > output_size) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    hash_object = (PUCHAR)calloc(hash_object_size, sizeof(UCHAR));
    if (!hash_object) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    status = BCryptCreateHash(alg, &hash, hash_object, hash_object_size, NULL, 0, 0);
    if (status >= 0) {
        status = BCryptHashData(hash, (PUCHAR)input, (ULONG)input_size, 0);
    }
    if (status >= 0) {
        status = BCryptFinishHash(hash, output, (ULONG)hash_length, 0);
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    free(hash_object);
    BCryptCloseAlgorithmProvider(alg, 0);
    return status >= 0 ? 1 : 0;
}

static char* base64url_encode_alloc(const unsigned char* data, size_t data_size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char* output = NULL;
    size_t output_capacity = 0;
    size_t index = 0;
    size_t used = 0;

    if (!data || data_size == 0) {
        return NULL;
    }

    output_capacity = ((data_size + 2) / 3) * 4 + 1;
    output = (char*)calloc(output_capacity, sizeof(char));
    if (!output) {
        return NULL;
    }

    for (index = 0; index < data_size; index += 3) {
        unsigned int value = (unsigned int)data[index] << 16;
        size_t remaining = data_size - index;

        if (remaining > 1) {
            value |= (unsigned int)data[index + 1] << 8;
        }
        if (remaining > 2) {
            value |= (unsigned int)data[index + 2];
        }

        output[used++] = table[(value >> 18) & 0x3F];
        output[used++] = table[(value >> 12) & 0x3F];
        if (remaining > 1) {
            output[used++] = table[(value >> 6) & 0x3F];
        }
        if (remaining > 2) {
            output[used++] = table[value & 0x3F];
        }
    }

    output[used] = '\0';
    return output;
}

static int generate_pkce_verifier(char* output, size_t output_size)
{
    unsigned char random_bytes[32];
    char* encoded = NULL;
    NTSTATUS status;

    if (!output || output_size == 0) {
        return 0;
    }
    output[0] = '\0';

    status = BCryptGenRandom(NULL, random_bytes, (ULONG)sizeof(random_bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        return 0;
    }

    encoded = base64url_encode_alloc(random_bytes, sizeof(random_bytes));
    if (!encoded) {
        return 0;
    }

    if (strlen(encoded) + 1 > output_size) {
        free(encoded);
        return 0;
    }

    strcpy_s(output, output_size, encoded);
    free(encoded);
    return 1;
}

static char* url_encode_alloc(const char* text)
{
    size_t length = 0;
    size_t used = 0;
    const unsigned char* p = (const unsigned char*)text;
    char* output = NULL;

    if (!text) {
        return NULL;
    }

    length = strlen(text);
    output = (char*)calloc(length * 3 + 1, sizeof(char));
    if (!output) {
        return NULL;
    }

    while (*p) {
        if ((*p >= 'A' && *p <= 'Z')
            || (*p >= 'a' && *p <= 'z')
            || (*p >= '0' && *p <= '9')
            || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            output[used++] = (char)*p;
        } else {
            sprintf_s(output + used, length * 3 + 1 - used, "%%%02X", *p);
            used += 3;
        }
        ++p;
    }
    output[used] = '\0';
    return output;
}

int pixiv_build_pkce_login_url(
    wchar_t* code_verifier,
    size_t code_verifier_count,
    wchar_t* login_url,
    size_t login_url_count,
    wchar_t* error_message,
    size_t error_message_count
)
{
    char verifier_utf8[128];
    unsigned char digest[32];
    char* challenge_utf8 = NULL;
    char login_url_utf8[1024];
    wchar_t* verifier_wide = NULL;
    wchar_t* login_url_wide = NULL;

    if (!code_verifier || code_verifier_count == 0 || !login_url || login_url_count == 0) {
        set_error(error_message, error_message_count, L"Pixiv login URL output buffer is not available.");
        return 0;
    }

    code_verifier[0] = L'\0';
    login_url[0] = L'\0';

    if (!generate_pkce_verifier(verifier_utf8, sizeof(verifier_utf8))) {
        set_error(error_message, error_message_count, L"Could not generate Pixiv PKCE verifier.");
        return 0;
    }

    if (!compute_sha256_bytes((const unsigned char*)verifier_utf8, strlen(verifier_utf8), digest, sizeof(digest))) {
        set_error(error_message, error_message_count, L"Could not compute Pixiv PKCE challenge.");
        return 0;
    }

    challenge_utf8 = base64url_encode_alloc(digest, sizeof(digest));
    if (!challenge_utf8) {
        set_error(error_message, error_message_count, L"Could not encode Pixiv PKCE challenge.");
        return 0;
    }

    snprintf(
        login_url_utf8,
        sizeof(login_url_utf8),
        PIXIV_AUTH_LOGIN_URL "?code_challenge=%s&code_challenge_method=S256&client=pixiv-android",
        challenge_utf8
    );
    free(challenge_utf8);

    verifier_wide = utf8_to_wide_alloc(verifier_utf8);
    login_url_wide = utf8_to_wide_alloc(login_url_utf8);
    if (!verifier_wide || !login_url_wide) {
        free(verifier_wide);
        free(login_url_wide);
        set_error(error_message, error_message_count, L"Could not convert Pixiv login URL.");
        return 0;
    }

    safe_wcs_copy(code_verifier, code_verifier_count, verifier_wide);
    safe_wcs_copy(login_url, login_url_count, login_url_wide);
    free(verifier_wide);
    free(login_url_wide);
    set_error(error_message, error_message_count, L"");
    return 1;
}

static void make_safe_filename(wchar_t* text)
{
    size_t length = 0;
    size_t index = 0;
    const wchar_t* invalid = L"<>:\"/\\|?*";

    if (!text) {
        return;
    }

    length = wcslen(text);
    for (index = 0; index < length; ++index) {
        if (text[index] < 32 || wcschr(invalid, text[index])) {
            text[index] = L'_';
        }
    }
    while (length > 0 && (text[length - 1] == L' ' || text[length - 1] == L'.')) {
        text[length - 1] = L'_';
        --length;
    }
}

static void replace_all(wchar_t* text, size_t text_count, const wchar_t* needle, const wchar_t* replacement)
{
    wchar_t buffer[2048];
    wchar_t* position = NULL;

    if (!text || !needle || !replacement || !needle[0]) {
        return;
    }

    for (;;) {
        position = wcsstr(text, needle);
        if (!position) {
            return;
        }

        buffer[0] = L'\0';
        *position = L'\0';
        wcscpy_s(buffer, 2048, text);
        wcscat_s(buffer, 2048, replacement);
        wcscat_s(buffer, 2048, position + wcslen(needle));
        safe_wcs_copy(text, text_count, buffer);
    }
}

static const char* skip_ws_local(const char* p)
{
    while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        ++p;
    }
    return p;
}

static const char* skip_string_value_local(const char* p)
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

static int extract_json_string_local(const char* p, char* output, size_t output_size)
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

static const char* skip_braced_value_local(const char* p, char open_char, char close_char)
{
    int depth = 0;

    if (!p || *p != open_char) {
        return p;
    }

    while (*p) {
        if (*p == '"') {
            p = skip_string_value_local(p);
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

static const char* skip_json_value_local(const char* p)
{
    p = skip_ws_local(p);
    if (!p) {
        return NULL;
    }
    if (*p == '"') {
        return skip_string_value_local(p);
    }
    if (*p == '{') {
        return skip_braced_value_local(p, '{', '}');
    }
    if (*p == '[') {
        return skip_braced_value_local(p, '[', ']');
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        ++p;
    }
    return p;
}

static int find_key_in_object_local(const char* json, const char* key, const char** value_start, const char** value_end)
{
    const char* p = skip_ws_local(json);
    char found_key[128];

    if (!p || *p != '{') {
        return 0;
    }
    ++p;

    for (;;) {
        size_t key_length = 0;

        p = skip_ws_local(p);
        if (!p || *p == '}') {
            return 0;
        }
        if (*p != '"') {
            return 0;
        }

        ++p;
        key_length = 0;
        while (p[key_length] && p[key_length] != '"' && key_length + 1 < sizeof(found_key)) {
            if (p[key_length] == '\\' && p[key_length + 1] != '\0') {
                found_key[key_length] = p[key_length + 1];
                ++key_length;
                ++p;
                continue;
            }
            found_key[key_length] = p[key_length];
            ++key_length;
        }
        found_key[key_length] = '\0';
        p = skip_string_value_local(p - 1);
        p = skip_ws_local(p);
        if (!p || *p != ':') {
            return 0;
        }
        ++p;
        p = skip_ws_local(p);
        if (!p) {
            return 0;
        }

        if (strcmp(found_key, key) == 0) {
            *value_start = p;
            *value_end = skip_json_value_local(p);
            return 1;
        }

        p = skip_json_value_local(p);
        p = skip_ws_local(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            return 0;
        }
    }
}

static char* json_extract_array_alloc_local(const char* json, const char* key)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t array_length = 0;
    char* array_text = NULL;

    if (!find_key_in_object_local(json, key, &value_start, &value_end)) {
        return NULL;
    }
    value_start = skip_ws_local(value_start);
    if (!value_start || *value_start != '[') {
        return NULL;
    }

    array_length = (size_t)(value_end - value_start);
    array_text = (char*)calloc(array_length + 1, sizeof(char));
    if (!array_text) {
        return NULL;
    }
    memcpy(array_text, value_start, array_length);
    array_text[array_length] = '\0';
    return array_text;
}

static char* json_extract_value_alloc_local(const char* json, const char* key)
{
    const char* value_start = NULL;
    const char* value_end = NULL;
    size_t value_length = 0;
    char* value_text = NULL;

    if (!find_key_in_object_local(json, key, &value_start, &value_end)) {
        return NULL;
    }

    value_length = (size_t)(value_end - value_start);
    value_text = (char*)calloc(value_length + 1, sizeof(char));
    if (!value_text) {
        return NULL;
    }

    memcpy(value_text, value_start, value_length);
    value_text[value_length] = '\0';
    return value_text;
}

static int pixiv_get_profile_page_size(const AppConfig* config)
{
    if (config && config->profile_page_size > 0) {
        return config->profile_page_size;
    }
    return PIXIV_DEFAULT_PROFILE_PAGE_SIZE;
}

static int json_collect_object_int_keys(const char* json_object, int** ids_out, int* count_out)
{
    const char* p = skip_ws_local(json_object);
    int* ids = NULL;
    int count = 0;
    int capacity = 0;

    if (ids_out) {
        *ids_out = NULL;
    }
    if (count_out) {
        *count_out = 0;
    }
    if (!p) {
        return 0;
    }
    if (*p == '[') {
        return 1;
    }
    if (*p != '{') {
        return 0;
    }

    ++p;
    while (*p) {
        char key_text[64];
        int key_value = 0;

        p = skip_ws_local(p);
        if (*p == '}') {
            break;
        }
        if (*p != '"') {
            free(ids);
            return 0;
        }
        if (!extract_json_string_local(p, key_text, sizeof(key_text))) {
            free(ids);
            return 0;
        }

        key_value = atoi(key_text);
        if (key_value > 0) {
            if (count >= capacity) {
                int new_capacity = capacity == 0 ? 32 : capacity * 2;
                int* grown = (int*)realloc(ids, (size_t)new_capacity * sizeof(int));
                if (!grown) {
                    free(ids);
                    return 0;
                }
                ids = grown;
                capacity = new_capacity;
            }
            ids[count++] = key_value;
        }

        p = skip_string_value_local(p);
        p = skip_ws_local(p);
        if (*p != ':') {
            free(ids);
            return 0;
        }
        ++p;
        p = skip_json_value_local(p);
        p = skip_ws_local(p);
        if (*p == ',') {
            ++p;
        }
    }

    if (ids_out) {
        *ids_out = ids;
    } else {
        free(ids);
    }
    if (count_out) {
        *count_out = count;
    }
    return 1;
}

static int parse_positive_int_token(const wchar_t* text, int* value_out, const wchar_t** end_out)
{
    const wchar_t* p = text;
    int value = 0;

    if (!p || *p < L'0' || *p > L'9') {
        return 0;
    }

    while (*p >= L'0' && *p <= L'9') {
        value = value * 10 + (int)(*p - L'0');
        ++p;
    }

    if (value_out) {
        *value_out = value;
    }
    if (end_out) {
        *end_out = p;
    }
    return value > 0;
}

static int try_parse_query_page_param(const wchar_t* target, int* page_out)
{
    const wchar_t* p = wcschr(target, L'?');

    if (page_out) {
        *page_out = 0;
    }
    if (!p) {
        return 0;
    }

    ++p;
    while (*p) {
        if (_wcsnicmp(p, L"p=", 2) == 0) {
            int page_value = 0;
            if (parse_positive_int_token(p + 2, &page_value, NULL)) {
                if (page_out) {
                    *page_out = page_value;
                }
                return 1;
            }
            return -1;
        }

        while (*p && *p != L'&' && *p != L'#') {
            ++p;
        }
        if (*p == L'&') {
            ++p;
            continue;
        }
        break;
    }

    return 0;
}

static int pixiv_parse_user_artworks_page_target(
    const wchar_t* target,
    int* user_id_out,
    int* page_out,
    PixivProfilePageTargetType* page_target_type_out
)
{
    const wchar_t* p = NULL;
    const wchar_t* segment_end = NULL;
    int user_id = 0;
    int page_value = 0;
    int page_parse_result = 0;
    PixivProfilePageTargetType target_type = PIXIV_PROFILE_PAGE_TARGET_NONE;

    if (!target || !user_id_out || !page_out || !page_target_type_out) {
        return 0;
    }

    while (*target == L' ') {
        ++target;
    }

    if (_wcsnicmp(target, L"users/", 6) == 0) {
        p = target + 6;
    } else {
        p = wcsstr(target, L"/users/");
        if (!p) {
            return 0;
        }
        p += wcslen(L"/users/");
    }

    if (!parse_positive_int_token(p, &user_id, &segment_end)) {
        return 0;
    }
    p = segment_end;
    if (*p != L'/') {
        return 0;
    }
    ++p;

    if (_wcsnicmp(p, L"artworks", 8) == 0) {
        target_type = PIXIV_PROFILE_PAGE_TARGET_ARTWORKS;
        p += 8;
    } else if (_wcsnicmp(p, L"illustrations", 13) == 0) {
        target_type = PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS;
        p += 13;
    } else if (_wcsnicmp(p, L"manga", 5) == 0) {
        target_type = PIXIV_PROFILE_PAGE_TARGET_MANGA;
        p += 5;
    } else if (_wcsnicmp(p, L"novels", 6) == 0) {
        target_type = PIXIV_PROFILE_PAGE_TARGET_NOVELS;
        p += 6;
    } else {
        return 0;
    }

    if (*p == L'/') {
        ++p;
    }
    if (*p != L'\0' && *p != L'?' && *p != L'#') {
        return 0;
    }

    page_parse_result = try_parse_query_page_param(target, &page_value);
    if (page_parse_result < 0) {
        page_value = 1;
    } else if (page_parse_result == 0) {
        page_value = 0;
    }
    *user_id_out = user_id;
    *page_out = page_value;
    *page_target_type_out = target_type;
    return 1;
}

static int pixiv_parse_user_profile_target(const wchar_t* target, int* user_id_out)
{
    const wchar_t* p = NULL;
    const wchar_t* segment_end = NULL;
    int user_id = 0;

    if (!target || !user_id_out) {
        return 0;
    }

    while (*target == L' ') {
        ++target;
    }

    if (_wcsnicmp(target, L"users/", 6) == 0) {
        p = target + 6;
    } else {
        p = wcsstr(target, L"/users/");
        if (!p) {
            return 0;
        }
        p += wcslen(L"/users/");
    }

    if (!parse_positive_int_token(p, &user_id, &segment_end)) {
        return 0;
    }

    p = segment_end;
    if (*p == L'/') {
        ++p;
        if (*p == L'\0' || *p == L'?' || *p == L'#') {
            *user_id_out = user_id;
            return 1;
        }
        return 0;
    }

    if (*p == L'\0' || *p == L'?' || *p == L'#') {
        *user_id_out = user_id;
        return 1;
    }

    return 0;
}

static const wchar_t* pixiv_profile_page_target_type_name(PixivProfilePageTargetType target_type)
{
    switch (target_type) {
    case PIXIV_PROFILE_PAGE_TARGET_ARTWORKS:
        return L"artworks";
    case PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS:
        return L"illustrations";
    case PIXIV_PROFILE_PAGE_TARGET_MANGA:
        return L"manga";
    case PIXIV_PROFILE_PAGE_TARGET_NOVELS:
        return L"novels";
    default:
        return L"artworks";
    }
}

static void build_canonical_artwork_url(int illust_id, wchar_t* output, size_t output_count)
{
    if (!output || output_count == 0) {
        return;
    }

    swprintf_s(output, output_count, L"https://www.pixiv.net/artworks/%d", illust_id);
}

static void build_canonical_novel_url(int novel_id, wchar_t* output, size_t output_count)
{
    if (!output || output_count == 0) {
        return;
    }
    swprintf_s(output, output_count, L"https://www.pixiv.net/novel/show.php?id=%d", novel_id);
}

static int pixiv_extract_novel_id(const wchar_t* target, int* novel_id_out)
{
    const wchar_t* p = NULL;

    if (!target || !novel_id_out) {
        return 0;
    }
    p = wcsstr(target, L"/novel/show.php");
    if (p) {
        p = wcsstr(p, L"id=");
        if (p) {
            p += 3;
        }
    } else {
        p = wcsstr(target, L"/novels/");
        if (p) {
            p += wcslen(L"/novels/");
        }
    }
    if (!p || *p < L'0' || *p > L'9') {
        return 0;
    }
    *novel_id_out = _wtoi(p);
    return *novel_id_out > 0;
}

static int append_wide_line_buffer(wchar_t** buffer, size_t* capacity, size_t* used, const wchar_t* line)
{
    size_t line_length = 0;
    size_t required = 0;
    wchar_t* grown = NULL;

    if (!buffer || !capacity || !used || !line) {
        return 0;
    }

    line_length = wcslen(line);
    required = *used + (*used > 0 ? 2 : 0) + line_length + 1;
    if (required > *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 256;
        while (new_capacity < required) {
            new_capacity *= 2;
        }
        grown = (wchar_t*)realloc(*buffer, new_capacity * sizeof(wchar_t));
        if (!grown) {
            return 0;
        }
        *buffer = grown;
        *capacity = new_capacity;
    }

    if (*used > 0) {
        (*buffer)[(*used)++] = L'\r';
        (*buffer)[(*used)++] = L'\n';
    }

    memcpy(*buffer + *used, line, (line_length + 1) * sizeof(wchar_t));
    *used += line_length;
    return 1;
}

static void ensure_parent_directory_for_file(const wchar_t* file_path)
{
    wchar_t directory[PATH_BUFFER_COUNT];

    if (!file_path || !file_path[0]) {
        return;
    }

    safe_wcs_copy(directory, PATH_BUFFER_COUNT, file_path);
    PathRemoveFileSpecW(directory);
    SHCreateDirectoryExW(NULL, directory, NULL);
}

static int write_archive_json_atomic(const wchar_t* archive_path, const char* json_text)
{
    wchar_t temporary_path[PATH_BUFFER_COUNT];

    if (!archive_path || !archive_path[0] || !json_text) {
        return 0;
    }
    if (swprintf_s(
            temporary_path,
            _countof(temporary_path),
            L"%ls.tmp.%lu.%lu",
            archive_path,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long)GetCurrentThreadId()) < 0) {
        return 0;
    }
    if (!write_text_file_utf8(temporary_path, json_text)) {
        return 0;
    }
    if (!MoveFileExW(temporary_path, archive_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary_path);
        return 0;
    }
    return 1;
}

static int archive_contains_canonical_url(const wchar_t* archive_path, const char* canonical_url_utf8)
{
    char* archive_json = NULL;
    char* array_text = NULL;
    const char* p = NULL;
    int found = 0;

    if (!archive_path || !archive_path[0] || !canonical_url_utf8 || !canonical_url_utf8[0]) {
        return 0;
    }

    AcquireSRWLockShared(&g_pixiv_archive_lock);
    archive_json = read_text_file_utf8(archive_path);
    if (!archive_json) {
        ReleaseSRWLockShared(&g_pixiv_archive_lock);
        return 0;
    }

    array_text = json_extract_array_alloc_local(archive_json, "downloaded_links");
    free(archive_json);
    if (!array_text) {
        ReleaseSRWLockShared(&g_pixiv_archive_lock);
        return 0;
    }

    p = skip_ws_local(array_text);
    if (*p == '[') {
        ++p;
    }

    while (*p) {
        char item[1024];

        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '"') {
            break;
        }
        if (!extract_json_string_local(p, item, sizeof(item))) {
            break;
        }
        if (strcmp(item, canonical_url_utf8) == 0) {
            found = 1;
            break;
        }

        p = skip_string_value_local(p);
        p = skip_ws_local(p);
        if (*p == ',') {
            ++p;
        }
    }

    free(array_text);
    ReleaseSRWLockShared(&g_pixiv_archive_lock);
    return found;
}

static int archive_append_canonical_url(const wchar_t* archive_path, const char* canonical_url_utf8)
{
    char* archive_json = NULL;
    char* array_text = NULL;
    char* output_json = NULL;
    const char* p = NULL;
    size_t capacity = 0;
    int first_item = 1;
    int exists = 0;
    char escaped[2048];

    if (!archive_path || !archive_path[0] || !canonical_url_utf8 || !canonical_url_utf8[0]) {
        return 0;
    }

    AcquireSRWLockExclusive(&g_pixiv_archive_lock);
    archive_json = read_text_file_utf8(archive_path);
    if (archive_json) {
        array_text = json_extract_array_alloc_local(archive_json, "downloaded_links");
    }

    capacity = (archive_json ? strlen(archive_json) : 0) + strlen(canonical_url_utf8) * 2 + 256;
    output_json = (char*)calloc(capacity, sizeof(char));
    if (!output_json) {
        free(array_text);
        free(archive_json);
        ReleaseSRWLockExclusive(&g_pixiv_archive_lock);
        return 0;
    }

    strcpy_s(output_json, capacity, "{\n  \"downloaded_links\": [\n");
    if (array_text) {
        p = skip_ws_local(array_text);
        if (*p == '[') {
            ++p;
        }

        while (*p) {
            char item[1024];
            char escaped_item[2048];

            p = skip_ws_local(p);
            if (*p == ']') {
                break;
            }
            if (*p != '"') {
                break;
            }
            if (!extract_json_string_local(p, item, sizeof(item))) {
                break;
            }

            if (strcmp(item, canonical_url_utf8) == 0) {
                exists = 1;
            }

            json_escape_string(item, escaped_item, sizeof(escaped_item));
            if (!first_item) {
                strcat_s(output_json, capacity, ",\n");
            }
            strcat_s(output_json, capacity, "    \"");
            strcat_s(output_json, capacity, escaped_item);
            strcat_s(output_json, capacity, "\"");
            first_item = 0;

            p = skip_string_value_local(p);
            p = skip_ws_local(p);
            if (*p == ',') {
                ++p;
            }
        }
    }

    if (!exists) {
        json_escape_string(canonical_url_utf8, escaped, sizeof(escaped));
        if (!first_item) {
            strcat_s(output_json, capacity, ",\n");
        }
        strcat_s(output_json, capacity, "    \"");
        strcat_s(output_json, capacity, escaped);
        strcat_s(output_json, capacity, "\"");
    }

    strcat_s(output_json, capacity, "\n  ]\n}\n");

    ensure_parent_directory_for_file(archive_path);
    exists = write_archive_json_atomic(archive_path, output_json);

    free(output_json);
    free(array_text);
    free(archive_json);
    ReleaseSRWLockExclusive(&g_pixiv_archive_lock);
    return exists;
}

static void expand_artist_folder_format(const AppConfig* config, const PixivArtworkInfo* info, wchar_t* output, size_t output_count)
{
    wchar_t user_id_text[32];
    wchar_t user_name_fallback[64];
    wchar_t user_account_fallback[64];

    safe_wcs_copy(output, output_count, config->artist_folder_format);
    swprintf_s(user_id_text, 32, L"%d", info->user_id);
    swprintf_s(user_name_fallback, _countof(user_name_fallback), L"pixiv_user_%d", info->user_id);
    swprintf_s(user_account_fallback, _countof(user_account_fallback), L"user_%d", info->user_id);
    replace_all(output, output_count, L"{user[name]}", info->user_name[0] ? info->user_name : user_name_fallback);
    replace_all(output, output_count, L"{user[account]}", info->user_account[0] ? info->user_account : user_account_fallback);
    replace_all(output, output_count, L"{user[id]}", user_id_text);
    make_safe_filename(output);
}

static void expand_single_filename_format(const AppConfig* config, const PixivArtworkInfo* info, const wchar_t* extension, wchar_t* output, size_t output_count)
{
    wchar_t id_text[32];
    wchar_t title_fallback[64];

    safe_wcs_copy(output, output_count, config->single_image_filename_format);
    swprintf_s(id_text, 32, L"%d", info->illust_id);
    swprintf_s(title_fallback, _countof(title_fallback), L"pixiv_illust_%d", info->illust_id);
    replace_all(output, output_count, L"{title}", info->title[0] ? info->title : title_fallback);
    replace_all(output, output_count, L"{id}", id_text);
    replace_all(output, output_count, L"{extension}", extension);
    make_safe_filename(output);
}

static void expand_multi_folder_format(const AppConfig* config, const PixivArtworkInfo* info, wchar_t* output, size_t output_count)
{
    wchar_t id_text[32];
    wchar_t title_fallback[64];

    safe_wcs_copy(output, output_count, config->multi_image_folder_format);
    swprintf_s(id_text, 32, L"%d", info->illust_id);
    swprintf_s(title_fallback, _countof(title_fallback), L"pixiv_illust_%d", info->illust_id);
    replace_all(output, output_count, L"{title}", info->title[0] ? info->title : title_fallback);
    replace_all(output, output_count, L"{id}", id_text);
    make_safe_filename(output);
}

static void replace_num_placeholders(wchar_t* text, size_t text_count, int page_number)
{
    wchar_t buffer[2048];
    wchar_t replacement[64];
    wchar_t token[64];
    wchar_t* start = NULL;
    wchar_t* end = NULL;

    if (!text) {
        return;
    }

    for (;;) {
        int width = 0;

        start = wcsstr(text, L"{num");
        if (!start) {
            return;
        }
        end = wcschr(start, L'}');
        if (!end) {
            return;
        }

        wcsncpy_s(token, 64, start, (size_t)(end - start + 1));
        if (swscanf_s(token, L"{num:>%d}", &width) != 1) {
            width = 0;
        }

        if (width > 0) {
            swprintf_s(replacement, 64, L"%0*d", width, page_number);
        } else {
            swprintf_s(replacement, 64, L"%d", page_number);
        }

        buffer[0] = L'\0';
        *start = L'\0';
        wcscpy_s(buffer, 2048, text);
        wcscat_s(buffer, 2048, replacement);
        wcscat_s(buffer, 2048, end + 1);
        safe_wcs_copy(text, text_count, buffer);
    }
}

static void expand_multi_filename_format(
    const AppConfig* config,
    const PixivArtworkInfo* info,
    const wchar_t* extension,
    int page_number,
    wchar_t* output,
    size_t output_count
)
{
    wchar_t id_text[32];
    wchar_t title_fallback[64];

    safe_wcs_copy(output, output_count, config->multi_image_filename_format);
    swprintf_s(id_text, 32, L"%d", info->illust_id);
    swprintf_s(title_fallback, _countof(title_fallback), L"pixiv_illust_%d", info->illust_id);
    replace_all(output, output_count, L"{title}", info->title[0] ? info->title : title_fallback);
    replace_all(output, output_count, L"{id}", id_text);
    replace_all(output, output_count, L"{extension}", extension);
    replace_num_placeholders(output, output_count, page_number);
    make_safe_filename(output);
}

static const wchar_t* ugoira_output_extension(const wchar_t* format);

static int pixiv_artwork_output_exists(
    const AppConfig* config,
    const PixivArtworkInfo* info,
    const wchar_t* base_path
)
{
    wchar_t extension[32];
    wchar_t filename[512];
    wchar_t folder_path[PATH_BUFFER_COUNT];
    wchar_t output_path[PATH_BUFFER_COUNT];
    int page_index = 0;
    int existing_count = 0;

    if (!config || !info || !base_path || !base_path[0]) {
        return 0;
    }

    if (_wcsicmp(info->illust_type, L"ugoira") == 0) {
        if (_wcsicmp(config->ugoira_format, L"skip") == 0) {
            return 0;
        }
        safe_wcs_copy(extension, _countof(extension), ugoira_output_extension(config->ugoira_format));
        expand_single_filename_format(config, info, extension, filename, _countof(filename));
        if (!filename[0]) {
            swprintf_s(filename, _countof(filename), L"%d.%ls", info->illust_id, extension);
        }
        safe_wcs_copy(output_path, _countof(output_path), base_path);
        PathAppendW(output_path, filename);
        return regular_file_exists(output_path);
    }

    if (info->page_count <= 1) {
        const wchar_t* primary_url = info->original_image_url[0] ? info->original_image_url : NULL;
        if ((!primary_url || !primary_url[0]) && info->page_url_count > 0) {
            primary_url = info->page_urls[0];
        }
        if (!primary_url || !primary_url[0]) {
            return 0;
        }
        if (!get_url_extension(primary_url, extension, _countof(extension))) {
            safe_wcs_copy(extension, _countof(extension), L"jpg");
        }
        expand_single_filename_format(config, info, extension, filename, _countof(filename));
        if (!filename[0]) {
            swprintf_s(filename, _countof(filename), L"%d.%ls", info->illust_id, extension);
        }
        safe_wcs_copy(output_path, _countof(output_path), base_path);
        PathAppendW(output_path, filename);
        return regular_file_exists(output_path);
    }

    safe_wcs_copy(folder_path, _countof(folder_path), base_path);
    expand_multi_folder_format(config, info, filename, _countof(filename));
    if (filename[0]) {
        PathAppendW(folder_path, filename);
    }

    for (page_index = 0; page_index < info->page_url_count; ++page_index) {
        const wchar_t* page_url = info->page_urls[page_index];
        int page_number = page_index + 1;

        if (!page_url || !page_url[0]) {
            continue;
        }
        if (!get_url_extension(page_url, extension, _countof(extension))) {
            safe_wcs_copy(extension, _countof(extension), L"jpg");
        }
        expand_multi_filename_format(config, info, extension, page_number, filename, _countof(filename));
        if (!filename[0]) {
            swprintf_s(filename, _countof(filename), L"%d_%02d.%ls", info->illust_id, page_number, extension);
        }
        safe_wcs_copy(output_path, _countof(output_path), folder_path);
        PathAppendW(output_path, filename);
        if (!regular_file_exists(output_path)) {
            return 0;
        }
        ++existing_count;
    }

    return existing_count > 0;
}

static void ensure_directory_for_file(const wchar_t* file_path)
{
    wchar_t directory[PATH_BUFFER_COUNT];
    safe_wcs_copy(directory, PATH_BUFFER_COUNT, file_path);
    PathRemoveFileSpecW(directory);
    SHCreateDirectoryExW(NULL, directory, NULL);
}

static void copy_json_string_to_wide(const char* json, const char* key, wchar_t* output, size_t output_count)
{
    char buffer[1024];

    if (!output || output_count == 0) {
        return;
    }

    output[0] = L'\0';
    ZeroMemory(buffer, sizeof(buffer));

    if (json_get_string(json, key, buffer, sizeof(buffer))) {
        wchar_t* wide = utf8_to_wide_alloc(buffer);
        if (wide) {
            safe_wcs_copy(output, output_count, wide);
            free(wide);
        }
    }
}

static int collect_meta_page_original_urls(const char* illust_object, PixivArtworkInfo* info)
{
    char* meta_pages_array = NULL;
    const char* p = NULL;
    int count = 0;

    if (!info || info->page_count <= 0) {
        return 1;
    }

    meta_pages_array = json_extract_array_alloc_local(illust_object, "meta_pages");
    if (!meta_pages_array) {
        return 1;
    }

    info->page_urls = (wchar_t(*)[1024])calloc((size_t)info->page_count, sizeof(*info->page_urls));
    if (!info->page_urls) {
        free(meta_pages_array);
        return 0;
    }

    p = skip_ws_local(meta_pages_array);
    if (*p == '[') {
        ++p;
    }

    while (*p) {
        char* page_object = NULL;
        char* image_urls_object = NULL;
        const char* object_end = NULL;
        size_t object_length = 0;

        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '{') {
            p = skip_json_value_local(p);
            if (*p == ',') {
                ++p;
            }
            continue;
        }

        object_end = skip_braced_value_local(p, '{', '}');
        object_length = (size_t)(object_end - p);
        page_object = (char*)calloc(object_length + 1, sizeof(char));
        if (!page_object) {
            free(info->page_urls);
            info->page_urls = NULL;
            free(meta_pages_array);
            return 0;
        }
        memcpy(page_object, p, object_length);
        page_object[object_length] = '\0';

        image_urls_object = json_extract_object_alloc(page_object, "image_urls");
        if (image_urls_object && count < info->page_count) {
            copy_json_string_to_wide(image_urls_object, "original", info->page_urls[count], 1024);
            if (info->page_urls[count][0]) {
                ++count;
            }
        }

        free(image_urls_object);
        free(page_object);
        p = skip_ws_local(object_end);
        if (*p == ',') {
            ++p;
        }
    }

    info->page_url_count = count;
    if (count == 0) {
        free(info->page_urls);
        info->page_urls = NULL;
    }
    free(meta_pages_array);
    return 1;
}

void pixiv_artwork_info_free(PixivArtworkInfo* info)
{
    if (!info) {
        return;
    }
    free(info->page_urls);
    info->page_urls = NULL;
    info->page_url_count = 0;
}

static void pixiv_ugoira_info_free(PixivUgoiraInfo* info)
{
    if (!info) {
        return;
    }
    free(info->frames);
    info->frames = NULL;
    info->frame_count = 0;
    info->zip_url[0] = L'\0';
}

static int pixiv_fetch_ugoira_metadata(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    int illust_id,
    PixivUgoiraInfo* info,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t url[512];
    wchar_t headers[2048];
    char* metadata_object = NULL;
    char* zip_urls_object = NULL;
    char* frames_array = NULL;
    const char* p = NULL;
    int frame_count = 0;
    int attempt = 0;
    int refreshed_token = 0;

    if (!info) {
        set_error(error_message, error_message_count, L"Ugoira metadata output is not available.");
        return 0;
    }
    if (!access_token || !access_token[0]) {
        set_error(error_message, error_message_count, L"Pixiv access token is empty.");
        return 0;
    }

    ZeroMemory(info, sizeof(*info));
    swprintf_s(url, _countof(url), L"https://app-api.pixiv.net/v1/ugoira/metadata?illust_id=%d", illust_id);
    for (attempt = 0; attempt < PIXIV_HTTP_RETRY_COUNT; ++attempt) {
        HttpResponse response;

        ZeroMemory(&response, sizeof(response));
        swprintf_s(
            headers,
            _countof(headers),
            L"User-Agent: PixivIOSApp/7.19.1 (iOS 16.7.2; iPhone12,8)\r\n"
            L"App-OS: ios\r\n"
            L"App-OS-Version: 16.7.2\r\n"
            L"App-Version: 7.19.1\r\n"
            L"Referer: https://app-api.pixiv.net/\r\n"
            L"Authorization: Bearer %ls\r\n",
            access_token
        );

        if (!http_request_utf8(L"GET", url, headers, NULL, 0, &response, error_message, error_message_count)) {
            if (attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }
            set_nested_http_diagnostic(L"Pixiv ugoira metadata request failed.", error_message);
            return 0;
        }
        if (pixiv_should_refresh_access_token_for_status(response.status_code)
            && !refreshed_token
            && config
            && config->refresh_token[0]
            && access_token_count > 0
            && pixiv_refresh_access_token(config, access_token, access_token_count, error_message, error_message_count)) {
            refreshed_token = 1;
            http_response_free(&response);
            pixiv_sleep_before_retry(config, attempt + 1);
            continue;
        }
        if (response.status_code < 200 || response.status_code >= 300 || !response.body) {
            if (pixiv_should_retry_http_status(response.status_code) && attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                http_response_free(&response);
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }
            set_http_status_diagnostic(L"Pixiv ugoira metadata request failed.", L"GET", url, &response);
            http_response_free(&response);
            set_error(error_message, error_message_count, L"Pixiv ugoira metadata request failed.");
            return 0;
        }

        metadata_object = json_extract_object_alloc(response.body, "ugoira_metadata");
        http_response_free(&response);
        break;
    }

    if (!metadata_object) {
        set_last_diagnostic_text(L"Could not parse Pixiv ugoira metadata. The response body did not contain ugoira_metadata.");
        set_error(error_message, error_message_count, L"Could not parse Pixiv ugoira metadata.");
        return 0;
    }

    zip_urls_object = json_extract_object_alloc(metadata_object, "zip_urls");
    if (zip_urls_object) {
        copy_json_string_to_wide(zip_urls_object, "medium", info->zip_url, _countof(info->zip_url));
    }
    if (!info->zip_url[0]) {
        free(zip_urls_object);
        free(metadata_object);
        set_error(error_message, error_message_count, L"Could not find Pixiv ugoira zip URL.");
        return 0;
    }

    frames_array = json_extract_array_alloc_local(metadata_object, "frames");
    free(zip_urls_object);
    free(metadata_object);
    if (!frames_array) {
        set_last_diagnostic_text(L"Could not parse Pixiv ugoira frames. The metadata did not include a frames array.");
        set_error(error_message, error_message_count, L"Could not parse Pixiv ugoira frames.");
        return 0;
    }

    p = skip_ws_local(frames_array);
    if (*p == '[') {
        ++p;
    }
    while (*p) {
        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p == '{') {
            ++frame_count;
            p = skip_braced_value_local(p, '{', '}');
        } else {
            p = skip_json_value_local(p);
        }
        p = skip_ws_local(p);
        if (*p == ',') {
            ++p;
        }
    }

    if (frame_count <= 0) {
        free(frames_array);
        set_error(error_message, error_message_count, L"Pixiv ugoira frame list is empty.");
        return 0;
    }

    info->frames = (PixivUgoiraFrame*)calloc((size_t)frame_count, sizeof(PixivUgoiraFrame));
    if (!info->frames) {
        free(frames_array);
        set_error(error_message, error_message_count, L"Could not allocate Pixiv ugoira frames.");
        return 0;
    }

    p = skip_ws_local(frames_array);
    if (*p == '[') {
        ++p;
    }

    info->frame_count = 0;
    while (*p && info->frame_count < frame_count) {
        char* frame_object = NULL;
        const char* object_end = NULL;
        size_t object_length = 0;

        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '{') {
            p = skip_json_value_local(p);
            if (*p == ',') {
                ++p;
            }
            continue;
        }

        object_end = skip_braced_value_local(p, '{', '}');
        object_length = (size_t)(object_end - p);
        frame_object = (char*)calloc(object_length + 1, sizeof(char));
        if (!frame_object) {
            free(frames_array);
            pixiv_ugoira_info_free(info);
            set_error(error_message, error_message_count, L"Could not allocate Pixiv ugoira frame object.");
            return 0;
        }
        memcpy(frame_object, p, object_length);
        frame_object[object_length] = '\0';

        copy_json_string_to_wide(frame_object, "file", info->frames[info->frame_count].file, _countof(info->frames[info->frame_count].file));
        info->frames[info->frame_count].delay = json_get_int(frame_object, "delay", 60);
        if (info->frames[info->frame_count].delay <= 0) {
            info->frames[info->frame_count].delay = 60;
        }
        free(frame_object);

        if (info->frames[info->frame_count].file[0]) {
            ++info->frame_count;
        }

        p = skip_ws_local(object_end);
        if (*p == ',') {
            ++p;
        }
    }

    free(frames_array);
    if (info->frame_count <= 0) {
        pixiv_ugoira_info_free(info);
        set_error(error_message, error_message_count, L"Could not parse Pixiv ugoira frame entries.");
        return 0;
    }

    set_error(error_message, error_message_count, L"");
    return 1;
}

static int get_url_extension(const wchar_t* url, wchar_t* extension, size_t extension_count)
{
    const wchar_t* last_dot = NULL;
    const wchar_t* p = NULL;
    if (!url || !extension || extension_count == 0) {
        return 0;
    }

    last_dot = wcsrchr(url, L'.');
    if (!last_dot) {
        return 0;
    }
    p = last_dot + 1;
    while (*p && *p != L'?' && *p != L'&' && *p != L'#' && *p != L'/') {
        ++p;
    }
    wcsncpy_s(extension, extension_count, last_dot + 1, (size_t)(p - (last_dot + 1)));
    return extension[0] != L'\0';
}

static int pixiv_refresh_access_token(const AppConfig* config, wchar_t* access_token, size_t access_token_count, wchar_t* error_message, size_t error_message_count)
{
    char time_text[64];
    char hash_input[256];
    char hash_hex[64];
    char* refresh_token_utf8 = NULL;
    char* encoded_refresh_token = NULL;
    char request_body[4096];
    wchar_t headers[1024];
    HttpResponse response;
    char* response_object = NULL;
    char token_utf8[1024];
    wchar_t* token_wide = NULL;
    const wchar_t* token_url = L"https://oauth.secure.pixiv.net/auth/token";

    if (!config->refresh_token[0]) {
        set_last_diagnostic_text(L"No Pixiv refresh token is configured in config.json.");
        set_error(error_message, error_message_count, L"No Pixiv refresh token is configured.");
        return 0;
    }

    get_utc_time_string(time_text, sizeof(time_text));
    snprintf(hash_input, sizeof(hash_input), "%s%s", time_text, PIXIV_HASH_SECRET);
    if (!compute_md5_hex(hash_input, hash_hex, sizeof(hash_hex))) {
        set_error(error_message, error_message_count, L"Could not compute Pixiv client hash.");
        return 0;
    }

    refresh_token_utf8 = wide_to_utf8_alloc(config->refresh_token);
    if (!refresh_token_utf8) {
        set_error(error_message, error_message_count, L"Could not convert Pixiv refresh token to UTF-8.");
        return 0;
    }

    encoded_refresh_token = url_encode_alloc(refresh_token_utf8);
    if (!encoded_refresh_token) {
        free(refresh_token_utf8);
        set_error(error_message, error_message_count, L"Could not encode Pixiv refresh token.");
        return 0;
    }
    snprintf(
        request_body,
        sizeof(request_body),
        "client_id=%s&client_secret=%s&grant_type=refresh_token&refresh_token=%s&get_secure_url=1",
        PIXIV_CLIENT_ID,
        PIXIV_CLIENT_SECRET,
        encoded_refresh_token ? encoded_refresh_token : ""
    );

    swprintf_s(
        headers,
        1024,
        L"User-Agent: PixivIOSApp/7.19.1 (iOS 16.7.2; iPhone12,8)\r\n"
        L"App-OS: ios\r\n"
        L"App-OS-Version: 16.7.2\r\n"
        L"App-Version: 7.19.1\r\n"
        L"Referer: https://app-api.pixiv.net/\r\n"
        L"Content-Type: application/x-www-form-urlencoded\r\n"
        L"X-Client-Time: %hs\r\n"
        L"X-Client-Hash: %hs\r\n",
        time_text,
        hash_hex
    );

    ZeroMemory(&response, sizeof(response));
    if (!http_request_utf8(L"POST", token_url, headers, request_body, (DWORD)strlen(request_body), &response, error_message, error_message_count)) {
        free(refresh_token_utf8);
        free(encoded_refresh_token);
        set_nested_http_diagnostic(L"Pixiv token refresh request failed.", error_message);
        return 0;
    }
    free(refresh_token_utf8);
    free(encoded_refresh_token);

    if (response.status_code < 200 || response.status_code >= 300 || !response.body) {
        set_http_status_diagnostic(L"Pixiv token refresh request failed.", L"POST", token_url, &response);
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Pixiv token refresh failed.");
        return 0;
    }

    response_object = json_extract_object_alloc(response.body, "response");
    if (!response_object || !json_get_string(response_object, "access_token", token_utf8, sizeof(token_utf8))) {
        set_http_status_diagnostic(L"Pixiv token refresh response could not be parsed.", L"POST", token_url, &response);
        free(response_object);
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Could not parse Pixiv access token.");
        return 0;
    }

    token_wide = utf8_to_wide_alloc(token_utf8);
    if (!token_wide) {
        free(response_object);
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Could not convert Pixiv access token.");
        return 0;
    }

    safe_wcs_copy(access_token, access_token_count, token_wide);
    free(token_wide);
    free(response_object);
    http_response_free(&response);
    set_error(error_message, error_message_count, L"");
    return 1;
}

int pixiv_prepare_access_token(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    wchar_t* error_message,
    size_t error_message_count
)
{
    return pixiv_refresh_access_token(config, access_token, access_token_count, error_message, error_message_count);
}

int pixiv_exchange_auth_code_for_refresh_token(
    const wchar_t* auth_code,
    const wchar_t* code_verifier,
    wchar_t* refresh_token,
    size_t refresh_token_count,
    wchar_t* error_message,
    size_t error_message_count
)
{
    char* auth_code_utf8 = NULL;
    char* code_verifier_utf8 = NULL;
    char* encoded_auth_code = NULL;
    char* encoded_code_verifier = NULL;
    char* encoded_redirect_uri = NULL;
    char request_body[4096];
    wchar_t headers[512];
    HttpResponse response;
    char token_utf8[1024];
    char error_utf8[256];
    char error_description_utf8[512];
    wchar_t* refresh_token_wide = NULL;
    const wchar_t* token_url = L"https://oauth.secure.pixiv.net/auth/token";

    if (!auth_code || !auth_code[0]) {
        set_last_diagnostic_text(L"Pixiv auth code is empty.");
        set_error(error_message, error_message_count, L"Pixiv auth code is empty.");
        return 0;
    }
    if (!code_verifier || !code_verifier[0]) {
        set_last_diagnostic_text(L"Pixiv PKCE verifier is missing. Open the auth button again and sign in once more.");
        set_error(error_message, error_message_count, L"Pixiv PKCE verifier is not ready. Open the auth button again and login once more.");
        return 0;
    }

    refresh_token[0] = L'\0';
    auth_code_utf8 = wide_to_utf8_alloc(auth_code);
    code_verifier_utf8 = wide_to_utf8_alloc(code_verifier);
    if (!auth_code_utf8 || !code_verifier_utf8) {
        free(auth_code_utf8);
        free(code_verifier_utf8);
        set_error(error_message, error_message_count, L"Could not convert Pixiv auth code.");
        return 0;
    }

    encoded_auth_code = url_encode_alloc(auth_code_utf8);
    encoded_code_verifier = url_encode_alloc(code_verifier_utf8);
    free(auth_code_utf8);
    free(code_verifier_utf8);
    encoded_redirect_uri = url_encode_alloc(PIXIV_AUTH_CALLBACK_URL);
    if (!encoded_auth_code || !encoded_code_verifier || !encoded_redirect_uri) {
        free(encoded_auth_code);
        free(encoded_code_verifier);
        free(encoded_redirect_uri);
        set_error(error_message, error_message_count, L"Could not encode Pixiv auth code.");
        return 0;
    }

    snprintf(
        request_body,
        sizeof(request_body),
        "client_id=%s&client_secret=%s&code=%s&code_verifier=%s&grant_type=authorization_code&include_policy=true&redirect_uri=%s",
        PIXIV_CLIENT_ID,
        PIXIV_CLIENT_SECRET,
        encoded_auth_code,
        encoded_code_verifier,
        encoded_redirect_uri
    );
    free(encoded_auth_code);
    free(encoded_code_verifier);
    free(encoded_redirect_uri);

    swprintf_s(
        headers,
        _countof(headers),
        L"User-Agent: PixivAndroidApp/5.0.234 (Android 11; Pixel 5)\r\n"
        L"Content-Type: application/x-www-form-urlencoded\r\n"
    );

    ZeroMemory(&response, sizeof(response));
    if (!http_request_utf8(L"POST", token_url, headers, request_body, (DWORD)strlen(request_body), &response, error_message, error_message_count)) {
        set_nested_http_diagnostic(L"Pixiv auth code exchange request failed.", error_message);
        return 0;
    }

    if (response.status_code < 200 || response.status_code >= 300 || !response.body) {
        set_http_status_diagnostic(L"Pixiv auth code exchange failed.", L"POST", token_url, &response);
        error_utf8[0] = '\0';
        error_description_utf8[0] = '\0';
        if (response.body) {
            json_get_string(response.body, "error", error_utf8, sizeof(error_utf8));
            json_get_string(response.body, "error_description", error_description_utf8, sizeof(error_description_utf8));
        }

        if (error_utf8[0] || error_description_utf8[0]) {
            wchar_t* error_wide = error_utf8[0] ? utf8_to_wide_alloc(error_utf8) : NULL;
            wchar_t* description_wide = error_description_utf8[0] ? utf8_to_wide_alloc(error_description_utf8) : NULL;
            wchar_t combined[768];

            if (error_wide && description_wide) {
                swprintf_s(combined, _countof(combined), L"Pixiv auth code exchange failed: %ls %ls", error_wide, description_wide);
            } else if (error_wide) {
                swprintf_s(combined, _countof(combined), L"Pixiv auth code exchange failed: %ls", error_wide);
            } else {
                swprintf_s(combined, _countof(combined), L"Pixiv auth code exchange failed: %ls", description_wide ? description_wide : L"");
            }
            free(error_wide);
            free(description_wide);
            set_error(error_message, error_message_count, combined);
        } else {
            set_error(error_message, error_message_count, L"Pixiv auth code exchange failed.");
        }
        http_response_free(&response);
        return 0;
    }

    if (!json_get_string(response.body, "refresh_token", token_utf8, sizeof(token_utf8))) {
        set_http_status_diagnostic(L"Could not parse Pixiv refresh token from auth response.", L"POST", token_url, &response);
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Could not parse Pixiv refresh token from auth response.");
        return 0;
    }

    refresh_token_wide = utf8_to_wide_alloc(token_utf8);
    http_response_free(&response);
    if (!refresh_token_wide) {
        set_error(error_message, error_message_count, L"Could not convert Pixiv refresh token.");
        return 0;
    }

    safe_wcs_copy(refresh_token, refresh_token_count, refresh_token_wide);
    free(refresh_token_wide);
    set_error(error_message, error_message_count, L"");
    return 1;
}

static int pixiv_fetch_artwork_info(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    int illust_id,
    PixivArtworkInfo* info,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t url[512];
    wchar_t headers[2048];
    char* illust_object = NULL;
    char* user_object = NULL;
    char* meta_single_page_object = NULL;
    int attempt = 0;
    int refreshed_token = 0;

    if (!access_token || !access_token[0]) {
        set_error(error_message, error_message_count, L"Pixiv access token is empty.");
        return 0;
    }

    ZeroMemory(info, sizeof(*info));
    info->illust_id = illust_id;

    swprintf_s(url, 512, L"https://app-api.pixiv.net/v1/illust/detail?illust_id=%d", illust_id);
    for (attempt = 0; attempt < PIXIV_HTTP_RETRY_COUNT; ++attempt) {
        HttpResponse response;

        ZeroMemory(&response, sizeof(response));
        swprintf_s(
            headers,
            2048,
            L"User-Agent: PixivIOSApp/7.19.1 (iOS 16.7.2; iPhone12,8)\r\n"
            L"App-OS: ios\r\n"
            L"App-OS-Version: 16.7.2\r\n"
            L"App-Version: 7.19.1\r\n"
            L"Referer: https://app-api.pixiv.net/\r\n"
            L"Authorization: Bearer %ls\r\n",
            access_token
        );

        if (!http_request_utf8(L"GET", url, headers, NULL, 0, &response, error_message, error_message_count)) {
            if (attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }
            set_nested_http_diagnostic(L"Pixiv illust detail request failed.", error_message);
            return 0;
        }

        if (pixiv_should_refresh_access_token_for_status(response.status_code)
            && !refreshed_token
            && config
            && config->refresh_token[0]
            && access_token_count > 0
            && pixiv_refresh_access_token(config, access_token, access_token_count, error_message, error_message_count)) {
            refreshed_token = 1;
            http_response_free(&response);
            pixiv_sleep_before_retry(config, attempt + 1);
            continue;
        }

        if (response.status_code < 200 || response.status_code >= 300 || !response.body) {
            if (pixiv_should_retry_http_status(response.status_code) && attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                http_response_free(&response);
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }
            set_http_status_diagnostic(L"Pixiv illust detail request failed.", L"GET", url, &response);
            http_response_free(&response);
            set_error(error_message, error_message_count, L"Pixiv illust detail request failed.");
            return 0;
        }

        illust_object = json_extract_object_alloc(response.body, "illust");
        if (!illust_object) {
            set_http_status_diagnostic(L"Could not parse Pixiv illust object.", L"GET", url, &response);
            http_response_free(&response);
            set_error(error_message, error_message_count, L"Could not parse Pixiv illust object.");
            return 0;
        }

        copy_json_string_to_wide(illust_object, "title", info->title, 256);
        copy_json_string_to_wide(illust_object, "type", info->illust_type, 32);
        info->page_count = json_get_int(illust_object, "page_count", 0);

        user_object = json_extract_object_alloc(illust_object, "user");
        if (user_object) {
            copy_json_string_to_wide(user_object, "name", info->user_name, 128);
            copy_json_string_to_wide(user_object, "account", info->user_account, 128);
            info->user_id = json_get_int(user_object, "id", 0);
        }

        meta_single_page_object = json_extract_object_alloc(illust_object, "meta_single_page");
        if (meta_single_page_object) {
            copy_json_string_to_wide(meta_single_page_object, "original_image_url", info->original_image_url, 1024);
        }
        if (!collect_meta_page_original_urls(illust_object, info)) {
            set_last_diagnostic_text(L"Could not parse Pixiv meta_pages. The illust response body did not contain usable original image URLs.");
            free(meta_single_page_object);
            free(user_object);
            free(illust_object);
            http_response_free(&response);
            pixiv_artwork_info_free(info);
            set_error(error_message, error_message_count, L"Could not parse Pixiv meta_pages.");
            return 0;
        }
        if (info->page_count > 1 && info->page_url_count != info->page_count) {
            set_last_diagnosticf(
                L"Pixiv returned only %d of %d image page URLs for illust_id=%d.",
                info->page_url_count,
                info->page_count,
                illust_id
            );
            free(meta_single_page_object);
            free(user_object);
            free(illust_object);
            http_response_free(&response);
            pixiv_artwork_info_free(info);
            ZeroMemory(info, sizeof(*info));
            info->illust_id = illust_id;
            if (attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }
            set_error(error_message, error_message_count, L"Pixiv returned an incomplete multi-page artwork response.");
            return 0;
        }

        free(meta_single_page_object);
        free(user_object);
        free(illust_object);
        http_response_free(&response);
        set_error(error_message, error_message_count, L"");
        return 1;
    }

    set_nested_http_diagnostic(L"Pixiv illust detail request failed.", error_message);
    set_error(error_message, error_message_count, L"Pixiv illust detail request failed.");
    return 0;
}

int pixiv_extract_artwork_id(const wchar_t* target, int* illust_id_out)
{
    const wchar_t* p = NULL;
    wchar_t digits[32];
    size_t used = 0;

    if (!target || !illust_id_out) {
        return 0;
    }

    while (*target == L' ') {
        ++target;
    }
    if (*target >= L'0' && *target <= L'9') {
        *illust_id_out = _wtoi(target);
        return *illust_id_out > 0;
    }

    p = wcsstr(target, L"/artworks/");
    if (!p) {
        p = wcsstr(target, L"illust_id=");
        if (p) {
            p += wcslen(L"illust_id=");
        }
    } else {
        p += wcslen(L"/artworks/");
    }
    if (!p) {
        return 0;
    }

    while (*p >= L'0' && *p <= L'9' && used + 1 < sizeof(digits) / sizeof(digits[0])) {
        digits[used++] = *p++;
    }
    digits[used] = L'\0';
    if (used == 0) {
        return 0;
    }
    *illust_id_out = _wtoi(digits);
    return *illust_id_out > 0;
}

static void build_app_api_headers(const wchar_t* access_token, wchar_t* headers, size_t headers_count)
{
    swprintf_s(
        headers,
        headers_count,
        L"User-Agent: PixivIOSApp/7.19.1 (iOS 16.7.2; iPhone12,8)\r\n"
        L"App-OS: ios\r\n"
        L"App-OS-Version: 16.7.2\r\n"
        L"App-Version: 7.19.1\r\n"
        L"Referer: https://app-api.pixiv.net/\r\n"
        L"Authorization: Bearer %ls\r\n",
        access_token
    );
}

static int pixiv_get_app_api_response(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    const wchar_t* url,
    const wchar_t* diagnostic_context,
    HttpResponse* response_out,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t headers[2048];
    int attempt = 0;
    int refreshed_token = 0;

    if (!access_token || !access_token[0] || !url || !response_out) {
        set_error(error_message, error_message_count, L"Invalid Pixiv API request arguments.");
        return 0;
    }

    for (attempt = 0; attempt < PIXIV_HTTP_RETRY_COUNT; ++attempt) {
        ZeroMemory(response_out, sizeof(*response_out));
        build_app_api_headers(access_token, headers, _countof(headers));
        if (!http_request_utf8(L"GET", url, headers, NULL, 0, response_out, error_message, error_message_count)) {
            if (attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }
            set_nested_http_diagnostic(diagnostic_context, error_message);
            return 0;
        }

        if (pixiv_should_refresh_access_token_for_status(response_out->status_code)
            && !refreshed_token
            && config
            && config->refresh_token[0]
            && access_token_count > 0
            && pixiv_refresh_access_token(config, access_token, access_token_count, error_message, error_message_count)) {
            refreshed_token = 1;
            http_response_free(response_out);
            pixiv_sleep_before_retry(config, attempt + 1);
            continue;
        }

        if (pixiv_should_retry_http_status(response_out->status_code) && attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
            http_response_free(response_out);
            pixiv_sleep_before_retry(config, attempt + 1);
            continue;
        }

        if (response_out->status_code < 200 || response_out->status_code >= 300 || !response_out->body) {
            set_http_status_diagnostic(diagnostic_context, L"GET", url, response_out);
            http_response_free(response_out);
            set_error(error_message, error_message_count, diagnostic_context);
            return 0;
        }

        set_error(error_message, error_message_count, L"");
        return 1;
    }

    set_error(error_message, error_message_count, diagnostic_context);
    return 0;
}

static char* extract_webview_novel_json_alloc(const char* html)
{
    const char* marker = NULL;
    const char* object_end = NULL;
    size_t object_length = 0;
    char* object_text = NULL;

    if (!html) {
        return NULL;
    }
    marker = strstr(html, "novel:");
    if (!marker) {
        return NULL;
    }
    marker += strlen("novel:");
    marker = skip_ws_local(marker);
    if (*marker != '{') {
        return NULL;
    }
    object_end = skip_braced_value_local(marker, '{', '}');
    if (!object_end || object_end <= marker || object_end[-1] != '}') {
        return NULL;
    }
    object_length = (size_t)(object_end - marker);
    object_text = (char*)calloc(object_length + 1, sizeof(char));
    if (!object_text) {
        return NULL;
    }
    memcpy(object_text, marker, object_length);
    object_text[object_length] = '\0';
    return object_text;
}

static void pixiv_novel_info_free(PixivNovelInfo* info)
{
    if (!info) {
        return;
    }
    free(info->text_utf8);
    free(info->webview_json);
    info->text_utf8 = NULL;
    info->webview_json = NULL;
}

static int pixiv_fetch_novel_info(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    int novel_id,
    PixivNovelInfo* info,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t url[1024];
    HttpResponse response;
    char* novel_object = NULL;
    char* user_object = NULL;
    char* image_urls_object = NULL;

    if (!info || novel_id <= 0) {
        set_error(error_message, error_message_count, L"Invalid Pixiv novel arguments.");
        return 0;
    }
    ZeroMemory(info, sizeof(*info));
    info->novel_id = novel_id;

    swprintf_s(url, _countof(url), L"https://app-api.pixiv.net/v2/novel/detail?novel_id=%d", novel_id);
    ZeroMemory(&response, sizeof(response));
    if (!pixiv_get_app_api_response(
            config,
            access_token,
            access_token_count,
            url,
            L"Pixiv novel detail request failed.",
            &response,
            error_message,
            error_message_count)) {
        return 0;
    }

    novel_object = json_extract_object_alloc(response.body, "novel");
    if (!novel_object) {
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Could not parse Pixiv novel detail.");
        return 0;
    }
    copy_json_string_to_wide(novel_object, "title", info->title, _countof(info->title));
    user_object = json_extract_object_alloc(novel_object, "user");
    if (user_object) {
        info->user_id = json_get_int(user_object, "id", 0);
        copy_json_string_to_wide(user_object, "name", info->user_name, _countof(info->user_name));
        copy_json_string_to_wide(user_object, "account", info->user_account, _countof(info->user_account));
    }
    image_urls_object = json_extract_object_alloc(novel_object, "image_urls");
    if (image_urls_object) {
        copy_json_string_to_wide(image_urls_object, "large", info->cover_url, _countof(info->cover_url));
        if (!info->cover_url[0]) {
            copy_json_string_to_wide(image_urls_object, "medium", info->cover_url, _countof(info->cover_url));
        }
    }
    free(image_urls_object);
    free(user_object);
    free(novel_object);
    http_response_free(&response);

    swprintf_s(
        url,
        _countof(url),
        L"https://app-api.pixiv.net/webview/v2/novel?id=%d&viewer_version=20221031_ai",
        novel_id
    );
    ZeroMemory(&response, sizeof(response));
    if (!pixiv_get_app_api_response(
            config,
            access_token,
            access_token_count,
            url,
            L"Pixiv novel text request failed.",
            &response,
            error_message,
            error_message_count)) {
        pixiv_novel_info_free(info);
        return 0;
    }

    info->webview_json = extract_webview_novel_json_alloc(response.body);
    http_response_free(&response);
    if (!info->webview_json) {
        set_error(error_message, error_message_count, L"Could not parse Pixiv novel webview data.");
        return 0;
    }
    info->text_utf8 = json_get_string_alloc(info->webview_json, "text");
    if (!info->text_utf8) {
        pixiv_novel_info_free(info);
        set_error(error_message, error_message_count, L"Could not parse Pixiv novel text.");
        return 0;
    }

    set_error(error_message, error_message_count, L"");
    return 1;
}

static int append_user_artwork_entry(
    PixivUserArtworkEntry** entries_out,
    int* count_out,
    int* capacity_out,
    int illust_id,
    const char* create_date
)
{
    PixivUserArtworkEntry* grown = NULL;
    int index = 0;

    if (!entries_out || !count_out || !capacity_out || illust_id <= 0) {
        return 0;
    }

    for (index = 0; index < *count_out; ++index) {
        if ((*entries_out)[index].illust_id == illust_id) {
            return 1;
        }
    }

    if (*count_out >= *capacity_out) {
        int new_capacity = *capacity_out == 0 ? 64 : *capacity_out * 2;
        grown = (PixivUserArtworkEntry*)realloc(*entries_out, (size_t)new_capacity * sizeof(PixivUserArtworkEntry));
        if (!grown) {
            return 0;
        }
        *entries_out = grown;
        *capacity_out = new_capacity;
    }

    (*entries_out)[*count_out].illust_id = illust_id;
    (*entries_out)[*count_out].create_date[0] = '\0';
    if (create_date) {
        strcpy_s((*entries_out)[*count_out].create_date, sizeof((*entries_out)[*count_out].create_date), create_date);
    }
    ++(*count_out);
    return 1;
}

static int pixiv_collect_user_artwork_entries_from_response(
    const char* response_json,
    const char* array_key,
    PixivUserArtworkEntry** entries_out,
    int* count_out,
    int* capacity_out
)
{
    char* illusts_array = NULL;
    const char* p = NULL;

    if (!response_json || !array_key || !entries_out || !count_out || !capacity_out) {
        return 0;
    }

    illusts_array = json_extract_array_alloc_local(response_json, array_key);
    if (!illusts_array) {
        return 0;
    }

    p = skip_ws_local(illusts_array);
    if (*p == '[') {
        ++p;
    }

    while (*p) {
        const char* object_end = NULL;
        size_t object_length = 0;
        char* item_object = NULL;
        int illust_id = 0;
        char create_date[64];

        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '{') {
            p = skip_json_value_local(p);
            if (*p == ',') {
                ++p;
            }
            continue;
        }

        object_end = skip_braced_value_local(p, '{', '}');
        object_length = (size_t)(object_end - p);
        item_object = (char*)calloc(object_length + 1, sizeof(char));
        if (!item_object) {
            free(illusts_array);
            return 0;
        }
        memcpy(item_object, p, object_length);
        item_object[object_length] = '\0';

        illust_id = json_get_int(item_object, "id", 0);
        create_date[0] = '\0';
        json_get_string(item_object, "create_date", create_date, sizeof(create_date));

        free(item_object);
        if (illust_id > 0 && !append_user_artwork_entry(entries_out, count_out, capacity_out, illust_id, create_date)) {
            free(illusts_array);
            return 0;
        }

        p = skip_ws_local(object_end);
        if (*p == ',') {
            ++p;
        }
    }

    free(illusts_array);
    return 1;
}

static int compare_entry_dates_desc(const PixivUserArtworkEntry* left, const PixivUserArtworkEntry* right)
{
    int cmp = strcmp(left->create_date, right->create_date);
    if (cmp > 0) {
        return -1;
    }
    if (cmp < 0) {
        return 1;
    }
    if (left->illust_id > right->illust_id) {
        return -1;
    }
    if (left->illust_id < right->illust_id) {
        return 1;
    }
    return 0;
}

static int pixiv_fetch_user_artwork_entries_for_type(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    int user_id,
    const wchar_t* api_type,
    PixivUserArtworkEntry** entries_out,
    int* count_out,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t* next_url = NULL;
    wchar_t url[1024];
    wchar_t headers[2048];
    PixivUserArtworkEntry* entries = NULL;
    int count = 0;
    int capacity = 0;
    int refreshed_token = 0;
    int page_count = 0;

    if (entries_out) {
        *entries_out = NULL;
    }
    if (count_out) {
        *count_out = 0;
    }
    if (!access_token || !access_token[0]) {
        set_error(error_message, error_message_count, L"Pixiv access token is empty.");
        return 0;
    }

    if (_wcsicmp(api_type, L"novel") == 0) {
        swprintf_s(url, _countof(url), L"https://app-api.pixiv.net/v1/user/novels?user_id=%d&filter=for_ios", user_id);
    } else {
        swprintf_s(
            url,
            _countof(url),
            L"https://app-api.pixiv.net/v1/user/illusts?user_id=%d&type=%ls&filter=for_ios",
            user_id,
            api_type
        );
    }

    for (;;) {
        HttpResponse response;
        char* next_url_utf8 = NULL;
        wchar_t* converted_next_url = NULL;
        int request_ok = 0;
        int attempt = 0;

        if (++page_count > 100000) {
            free(next_url);
            free(entries);
            set_error(error_message, error_message_count, L"Pixiv user list pagination exceeded the safety limit.");
            return 0;
        }

        ZeroMemory(&response, sizeof(response));
        for (attempt = 0; attempt < PIXIV_HTTP_RETRY_COUNT; ++attempt) {
            build_app_api_headers(access_token, headers, _countof(headers));
            ZeroMemory(&response, sizeof(response));

            if (!http_request_utf8(L"GET", next_url ? next_url : url, headers, NULL, 0, &response, error_message, error_message_count)) {
                if (attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                    pixiv_sleep_before_retry(config, attempt + 1);
                    continue;
                }
                set_nested_http_diagnostic(L"Pixiv user illusts request failed.", error_message);
                free(next_url);
                free(entries);
                return 0;
            }

            if (pixiv_should_refresh_access_token_for_status(response.status_code)
                && !refreshed_token
                && config
                && config->refresh_token[0]
                && access_token_count > 0
                && pixiv_refresh_access_token(config, access_token, access_token_count, error_message, error_message_count)) {
                refreshed_token = 1;
                http_response_free(&response);
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }

            if (pixiv_should_retry_http_status(response.status_code) && attempt + 1 < PIXIV_HTTP_RETRY_COUNT) {
                http_response_free(&response);
                pixiv_sleep_before_retry(config, attempt + 1);
                continue;
            }

            request_ok = 1;
            break;
        }

        if (!request_ok) {
            free(next_url);
            free(entries);
            set_error(error_message, error_message_count, L"Pixiv user illusts request failed.");
            return 0;
        }

        if (response.status_code < 200 || response.status_code >= 300 || !response.body) {
            set_http_status_diagnostic(L"Pixiv user illusts request failed.", L"GET", next_url ? next_url : url, &response);
            http_response_free(&response);
            free(next_url);
            free(entries);
            set_error(error_message, error_message_count, L"Pixiv user illusts request failed.");
            return 0;
        }

        if (!pixiv_collect_user_artwork_entries_from_response(
                response.body,
                _wcsicmp(api_type, L"novel") == 0 ? "novels" : "illusts",
                &entries,
                &count,
                &capacity)) {
            set_http_status_diagnostic(L"Could not parse Pixiv user illust list.", L"GET", next_url ? next_url : url, &response);
            http_response_free(&response);
            free(next_url);
            free(entries);
            set_error(error_message, error_message_count, L"Could not parse Pixiv user illust list.");
            return 0;
        }

        next_url_utf8 = json_get_string_alloc(response.body, "next_url");
        http_response_free(&response);

        if (!next_url_utf8 || !next_url_utf8[0]) {
            free(next_url_utf8);
            break;
        }
        converted_next_url = utf8_to_wide_alloc(next_url_utf8);
        free(next_url_utf8);
        if (!converted_next_url || _wcsnicmp(converted_next_url, L"https://", 8) != 0) {
            free(converted_next_url);
            free(next_url);
            free(entries);
            set_error(error_message, error_message_count, L"Pixiv returned an invalid next_url.");
            return 0;
        }
        if (next_url && _wcsicmp(next_url, converted_next_url) == 0) {
            free(converted_next_url);
            free(next_url);
            free(entries);
            set_error(error_message, error_message_count, L"Pixiv returned a repeated next_url.");
            return 0;
        }
        free(next_url);
        next_url = converted_next_url;
        sleep_between_requests(config);
    }

    free(next_url);

    if (entries_out) {
        *entries_out = entries;
    } else {
        free(entries);
    }
    if (count_out) {
        *count_out = count;
    }
    set_error(error_message, error_message_count, L"");
    return 1;
}

static int pixiv_build_profile_page_id_list(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    int user_id,
    PixivProfilePageTargetType page_target_type,
    int page_number,
    int** illust_ids_out,
    int* illust_id_count_out,
    wchar_t* error_message,
    size_t error_message_count
)
{
    PixivUserArtworkEntry* illust_entries = NULL;
    PixivUserArtworkEntry* manga_entries = NULL;
    PixivUserArtworkEntry* merged_entries = NULL;
    int illust_count = 0;
    int manga_count = 0;
    int merged_count = 0;
    int page_size = pixiv_get_profile_page_size(config);
    int fetch_all_pages = page_number <= 0 ? 1 : 0;
    int slice_start = 0;
    int slice_end = 0;
    int slice_count = 0;

    if (illust_ids_out) {
        *illust_ids_out = NULL;
    }
    if (illust_id_count_out) {
        *illust_id_count_out = 0;
    }

    if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_ARTWORKS
        || page_target_type == PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS) {
        if (!pixiv_fetch_user_artwork_entries_for_type(
                config,
                access_token,
                access_token_count,
                user_id,
                L"illust",
                &illust_entries,
                &illust_count,
                error_message,
                error_message_count)) {
            return 0;
        }
    }

    if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_NOVELS) {
        if (!pixiv_fetch_user_artwork_entries_for_type(
                config,
                access_token,
                access_token_count,
                user_id,
                L"novel",
                &illust_entries,
                &illust_count,
                error_message,
                error_message_count)) {
            return 0;
        }
    }

    if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_ARTWORKS
        || page_target_type == PIXIV_PROFILE_PAGE_TARGET_MANGA) {
        if (!pixiv_fetch_user_artwork_entries_for_type(
                config,
                access_token,
                access_token_count,
                user_id,
                L"manga",
                &manga_entries,
                &manga_count,
                error_message,
                error_message_count)) {
            free(illust_entries);
            return 0;
        }
    }

    if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_ARTWORKS) {
        int left = 0;
        int right = 0;
        int out = 0;

        merged_count = illust_count + manga_count;
        merged_entries = (PixivUserArtworkEntry*)calloc((size_t)merged_count, sizeof(PixivUserArtworkEntry));
        if (merged_count > 0 && !merged_entries) {
            free(illust_entries);
            free(manga_entries);
            set_error(error_message, error_message_count, L"Could not allocate Pixiv artwork merge buffer.");
            return 0;
        }

        while (left < illust_count || right < manga_count) {
            if (right >= manga_count
                || (left < illust_count && compare_entry_dates_desc(&illust_entries[left], &manga_entries[right]) <= 0)) {
                merged_entries[out++] = illust_entries[left++];
            } else {
                merged_entries[out++] = manga_entries[right++];
            }
        }
    } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS) {
        merged_entries = illust_entries;
        merged_count = illust_count;
        illust_entries = NULL;
    } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_MANGA) {
        merged_entries = manga_entries;
        merged_count = manga_count;
        manga_entries = NULL;
    } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_NOVELS) {
        merged_entries = illust_entries;
        merged_count = illust_count;
        illust_entries = NULL;
    }

    free(illust_entries);
    free(manga_entries);

    if (merged_count <= 0) {
        free(merged_entries);
        swprintf_s(
            error_message,
            error_message_count,
            L"No accessible %ls works were found for user %d.",
            pixiv_profile_page_target_type_name(page_target_type),
            user_id
        );
        return 0;
    }

    if (fetch_all_pages) {
        int index = 0;

        if (illust_ids_out) {
            *illust_ids_out = (int*)calloc((size_t)merged_count, sizeof(int));
            if (!*illust_ids_out) {
                free(merged_entries);
                set_error(error_message, error_message_count, L"Could not allocate Pixiv artwork ID buffer.");
                return 0;
            }
            for (index = 0; index < merged_count; ++index) {
                (*illust_ids_out)[index] = merged_entries[index].illust_id;
            }
        }
        if (illust_id_count_out) {
            *illust_id_count_out = merged_count;
        }
        free(merged_entries);
        set_error(error_message, error_message_count, L"");
        return 1;
    }

    slice_start = (page_number - 1) * page_size;
    if (slice_start >= merged_count) {
        free(merged_entries);
        swprintf_s(
            error_message,
            error_message_count,
            L"User %d %ls page %d is out of range.",
            user_id,
            pixiv_profile_page_target_type_name(page_target_type),
            page_number
        );
        return 0;
    }

    slice_end = slice_start + page_size;
    if (slice_end > merged_count) {
        slice_end = merged_count;
    }
    slice_count = slice_end - slice_start;

    if (illust_ids_out) {
        int index = 0;
        *illust_ids_out = (int*)calloc((size_t)slice_count, sizeof(int));
        if (!*illust_ids_out) {
            free(merged_entries);
            set_error(error_message, error_message_count, L"Could not allocate Pixiv page slice buffer.");
            return 0;
        }
        for (index = 0; index < slice_count; ++index) {
            (*illust_ids_out)[index] = merged_entries[slice_start + index].illust_id;
        }
    }
    if (illust_id_count_out) {
        *illust_id_count_out = slice_count;
    }

    free(merged_entries);
    set_error(error_message, error_message_count, L"");
    return 1;
}

int pixiv_expand_target_to_artwork_urls(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    const wchar_t* target,
    wchar_t** urls_text_out,
    int* url_count_out,
    wchar_t* error_message,
    size_t error_message_count
)
{
    int illust_id = 0;
    int user_id = 0;
    int page_number = 1;
    PixivProfilePageTargetType page_target_type = PIXIV_PROFILE_PAGE_TARGET_NONE;
    wchar_t canonical_target[256];
    wchar_t local_access_token[2048];
    wchar_t* token_to_use = access_token;
    wchar_t* lines = NULL;
    size_t capacity = 0;
    size_t used = 0;
    int count = 0;
    int parsed_page_target = 0;
    int parsed_profile_target = 0;

    if (urls_text_out) {
        *urls_text_out = NULL;
    }
    if (url_count_out) {
        *url_count_out = 0;
    }
    if (!target || !target[0]) {
        set_error(error_message, error_message_count, L"Pixiv target is empty.");
        return 0;
    }

    parsed_page_target = pixiv_parse_user_artworks_page_target(target, &user_id, &page_number, &page_target_type);
    if (!parsed_page_target) {
        parsed_profile_target = pixiv_parse_user_profile_target(target, &user_id);
    }

    if (parsed_page_target || parsed_profile_target) {
        PixivProfilePageTargetType requested_types[3];
        int requested_type_count = 0;
        int requested_index = 0;

        if (!token_to_use || !token_to_use[0]) {
            if (!pixiv_refresh_access_token(config, local_access_token, _countof(local_access_token), error_message, error_message_count)) {
                return 0;
            }
            token_to_use = local_access_token;
            access_token_count = _countof(local_access_token);
        }

        if (parsed_profile_target) {
            page_number = 0;
            if (config->download_illustrations) {
                requested_types[requested_type_count++] = PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS;
            }
            if (config->download_manga) {
                requested_types[requested_type_count++] = PIXIV_PROFILE_PAGE_TARGET_MANGA;
            }
            if (config->download_novels) {
                requested_types[requested_type_count++] = PIXIV_PROFILE_PAGE_TARGET_NOVELS;
            }
        } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_ARTWORKS) {
            if (config->download_illustrations && config->download_manga) {
                requested_types[requested_type_count++] = PIXIV_PROFILE_PAGE_TARGET_ARTWORKS;
            } else if (config->download_illustrations) {
                requested_types[requested_type_count++] = PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS;
            } else if (config->download_manga) {
                requested_types[requested_type_count++] = PIXIV_PROFILE_PAGE_TARGET_MANGA;
            }
        } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS && config->download_illustrations) {
            requested_types[requested_type_count++] = page_target_type;
        } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_MANGA && config->download_manga) {
            requested_types[requested_type_count++] = page_target_type;
        } else if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_NOVELS && config->download_novels) {
            requested_types[requested_type_count++] = page_target_type;
        }

        if (requested_type_count <= 0) {
            set_error(error_message, error_message_count, L"The Pixiv target type is not selected.");
            return 0;
        }

        for (requested_index = 0; requested_index < requested_type_count; ++requested_index) {
            int* work_ids = NULL;
            int work_id_count = 0;
            int index = 0;
            PixivProfilePageTargetType requested_type = requested_types[requested_index];

            if (!pixiv_build_profile_page_id_list(
                    config,
                    token_to_use,
                    access_token_count,
                    user_id,
                    requested_type,
                    page_number,
                    &work_ids,
                    &work_id_count,
                    error_message,
                    error_message_count)) {
                free(lines);
                return 0;
            }

            for (index = 0; index < work_id_count; ++index) {
                if (requested_type == PIXIV_PROFILE_PAGE_TARGET_NOVELS) {
                    build_canonical_novel_url(work_ids[index], canonical_target, _countof(canonical_target));
                } else {
                    build_canonical_artwork_url(work_ids[index], canonical_target, _countof(canonical_target));
                }
                if (!append_wide_line_buffer(&lines, &capacity, &used, canonical_target)) {
                    free(work_ids);
                    free(lines);
                    set_error(error_message, error_message_count, L"Could not allocate Pixiv target URL buffer.");
                    return 0;
                }
                ++count;
            }
            free(work_ids);
        }

        if (count <= 0) {
            free(lines);
            set_error(error_message, error_message_count, L"No accessible Pixiv works were found.");
            return 0;
        }

        if (urls_text_out) {
            *urls_text_out = lines;
        } else {
            free(lines);
        }
        if (url_count_out) {
            *url_count_out = count;
        }
        set_error(error_message, error_message_count, L"");
        return 1;
    }

    if (pixiv_extract_novel_id(target, &illust_id)) {
        if (!config->download_novels) {
            set_error(error_message, error_message_count, L"Novel downloads are not selected.");
            return 0;
        }
        build_canonical_novel_url(illust_id, canonical_target, _countof(canonical_target));
        if (!append_wide_line_buffer(&lines, &capacity, &used, canonical_target)) {
            set_error(error_message, error_message_count, L"Could not allocate Pixiv novel URL buffer.");
            free(lines);
            return 0;
        }
        if (urls_text_out) {
            *urls_text_out = lines;
        } else {
            free(lines);
        }
        if (url_count_out) {
            *url_count_out = 1;
        }
        set_error(error_message, error_message_count, L"");
        return 1;
    }

    if (!pixiv_extract_artwork_id(target, &illust_id)) {
        set_last_diagnosticf(L"Could not parse a Pixiv artwork ID from target: %ls", target);
        set_error(error_message, error_message_count, L"Could not parse Pixiv artwork ID.");
        return 0;
    }

    build_canonical_artwork_url(illust_id, canonical_target, _countof(canonical_target));
    if (!append_wide_line_buffer(&lines, &capacity, &used, canonical_target)) {
        set_error(error_message, error_message_count, L"Could not allocate Pixiv artwork URL buffer.");
        free(lines);
        return 0;
    }

    if (urls_text_out) {
        *urls_text_out = lines;
    } else {
        free(lines);
    }
    if (url_count_out) {
        *url_count_out = 1;
    }
    set_error(error_message, error_message_count, L"");
    return 1;
}

static void build_image_request_headers(wchar_t* image_headers, size_t image_headers_count)
{
    swprintf_s(
        image_headers,
        image_headers_count,
        L"User-Agent: PixivIOSApp/7.19.1 (iOS 16.7.2; iPhone12,8)\r\n"
        L"Referer: https://app-api.pixiv.net/\r\n"
    );
}

static int create_unique_temp_directory(wchar_t* output, size_t output_count, wchar_t* error_message, size_t error_message_count)
{
    wchar_t temp_path[PATH_BUFFER_COUNT];
    wchar_t temp_file[PATH_BUFFER_COUNT];

    if (!output || output_count == 0) {
        set_error(error_message, error_message_count, L"Temporary directory output is not available.");
        return 0;
    }

    if (GetTempPathW(PATH_BUFFER_COUNT, temp_path) == 0) {
        set_error(error_message, error_message_count, L"Could not get the system temp path.");
        return 0;
    }
    if (GetTempFileNameW(temp_path, L"pzc", 0, temp_file) == 0) {
        set_error(error_message, error_message_count, L"Could not create a Pixiv temp path.");
        return 0;
    }

    DeleteFileW(temp_file);
    if (!CreateDirectoryW(temp_file, NULL)) {
        set_error(error_message, error_message_count, L"Could not create a Pixiv temp directory.");
        return 0;
    }

    safe_wcs_copy(output, output_count, temp_file);
    set_error(error_message, error_message_count, L"");
    return 1;
}

static int pixiv_download_file_with_retry(
    const AppConfig* config,
    const wchar_t* url,
    const wchar_t* extra_headers,
    const wchar_t* output_path,
    const wchar_t* diagnostic_context,
    wchar_t* error_message,
    size_t error_message_count
)
{
    int attempt = 0;

    for (attempt = 0; attempt < PIXIV_FILE_RETRY_COUNT; ++attempt) {
        if (http_download_to_file(url, extra_headers, output_path, error_message, error_message_count)) {
            return 1;
        }

        DeleteFileW(output_path);
        if (attempt + 1 < PIXIV_FILE_RETRY_COUNT) {
            pixiv_sleep_before_retry(config, attempt + 1);
            continue;
        }
    }

    if (!g_pixiv_last_diagnostic[0] && diagnostic_context && diagnostic_context[0]) {
        set_last_diagnosticf(
            L"%ls | %ls",
            diagnostic_context,
            error_message && error_message[0] ? error_message : L"download failed"
        );
    }
    return 0;
}

static void remove_path_recursive(const wchar_t* path)
{
    SHFILEOPSTRUCTW operation;
    size_t length = 0;
    wchar_t double_null_path[PATH_BUFFER_COUNT + 2];

    if (!path || !path[0]) {
        return;
    }

    length = wcslen(path);
    if (length >= PATH_BUFFER_COUNT) {
        return;
    }

    ZeroMemory(&operation, sizeof(operation));
    ZeroMemory(double_null_path, sizeof(double_null_path));
    memcpy(double_null_path, path, (length + 1) * sizeof(wchar_t));
    double_null_path[length + 1] = L'\0';

    operation.wFunc = FO_DELETE;
    operation.pFrom = double_null_path;
    operation.fFlags = FOF_NOERRORUI | FOF_NOCONFIRMATION | FOF_SILENT;
    SHFileOperationW(&operation);
}

static int run_process_and_wait(
    const wchar_t* command_line,
    const wchar_t* working_directory,
    DWORD* exit_code_out,
    wchar_t* error_message,
    size_t error_message_count
)
{
    STARTUPINFOW startup_info;
    PROCESS_INFORMATION process_info;
    wchar_t* mutable_command_line = NULL;

    if (exit_code_out) {
        *exit_code_out = (DWORD)-1;
    }
    if (!command_line || !command_line[0]) {
        set_error(error_message, error_message_count, L"No process command line was provided.");
        return 0;
    }

    ZeroMemory(&startup_info, sizeof(startup_info));
    ZeroMemory(&process_info, sizeof(process_info));
    startup_info.cb = sizeof(startup_info);

    mutable_command_line = _wcsdup(command_line);
    if (!mutable_command_line) {
        set_error(error_message, error_message_count, L"Could not allocate the process command line.");
        return 0;
    }

    if (!CreateProcessW(NULL, mutable_command_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, working_directory && working_directory[0] ? working_directory : NULL, &startup_info, &process_info)) {
        set_last_diagnosticf(
            L"Could not start the helper process. GetLastError=%lu | Command: %ls | Working directory: %ls",
            (unsigned long)GetLastError(),
            command_line,
            working_directory && working_directory[0] ? working_directory : L"(default)"
        );
        free(mutable_command_line);
        set_error(error_message, error_message_count, L"Could not start the helper process.");
        return 0;
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    GetExitCodeProcess(process_info.hProcess, exit_code_out);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    free(mutable_command_line);

    if (exit_code_out && *exit_code_out != 0) {
        wchar_t message[256];
        swprintf_s(message, _countof(message), L"Helper process failed with exit code %lu.", (unsigned long)*exit_code_out);
        set_last_diagnosticf(
            L"Helper process failed with exit code %lu. Command: %ls | Working directory: %ls",
            (unsigned long)*exit_code_out,
            command_line,
            working_directory && working_directory[0] ? working_directory : L"(default)"
        );
        set_error(error_message, error_message_count, message);
        return 0;
    }

    set_error(error_message, error_message_count, L"");
    return 1;
}

static wchar_t* escape_powershell_single_quotes_alloc(const wchar_t* text)
{
    size_t length = 0;
    size_t quote_count = 0;
    size_t index = 0;
    size_t used = 0;
    wchar_t* output = NULL;

    if (!text) {
        return NULL;
    }

    length = wcslen(text);
    for (index = 0; index < length; ++index) {
        if (text[index] == L'\'') {
            ++quote_count;
        }
    }

    output = (wchar_t*)calloc(length + quote_count + 1, sizeof(wchar_t));
    if (!output) {
        return NULL;
    }

    for (index = 0; index < length; ++index) {
        output[used++] = text[index];
        if (text[index] == L'\'') {
            output[used++] = L'\'';
        }
    }
    output[used] = L'\0';
    return output;
}

static int expand_zip_archive(
    const wchar_t* zip_path,
    const wchar_t* destination_path,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t* escaped_zip_path = NULL;
    wchar_t* escaped_destination_path = NULL;
    wchar_t command_line[PATH_BUFFER_COUNT * 3];
    DWORD exit_code = 0;

    escaped_zip_path = escape_powershell_single_quotes_alloc(zip_path);
    escaped_destination_path = escape_powershell_single_quotes_alloc(destination_path);
    if (!escaped_zip_path || !escaped_destination_path) {
        free(escaped_zip_path);
        free(escaped_destination_path);
        set_error(error_message, error_message_count, L"Could not build the zip extraction command.");
        return 0;
    }

    swprintf_s(
        command_line,
        _countof(command_line),
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath '%ls' -DestinationPath '%ls' -Force\"",
        escaped_zip_path,
        escaped_destination_path
    );
    free(escaped_zip_path);
    free(escaped_destination_path);

    if (!run_process_and_wait(command_line, NULL, &exit_code, error_message, error_message_count)) {
        return 0;
    }

    set_error(error_message, error_message_count, L"");
    return 1;
}

static int write_ugoira_concat_file(
    const wchar_t* directory_path,
    const PixivUgoiraInfo* ugoira,
    wchar_t* concat_path,
    size_t concat_path_count,
    wchar_t* error_message,
    size_t error_message_count
)
{
    FILE* file = NULL;
    int index = 0;

    if (!directory_path || !ugoira || !ugoira->frames || ugoira->frame_count <= 0) {
        set_error(error_message, error_message_count, L"Could not build the ugoira concat input file.");
        return 0;
    }

    safe_wcs_copy(concat_path, concat_path_count, directory_path);
    PathAppendW(concat_path, L"ugoira.ffconcat");

    if (_wfopen_s(&file, concat_path, L"wb") != 0 || !file) {
        set_error(error_message, error_message_count, L"Could not create the ugoira concat file.");
        return 0;
    }

    for (index = 0; index < ugoira->frame_count; ++index) {
        char line[256];
        double duration = (double)ugoira->frames[index].delay / 1000.0;
        const wchar_t* frame_name = ugoira->frames[index].file;
        char* frame_name_utf8 = NULL;

        if (!frame_name[0]) {
            continue;
        }

        frame_name_utf8 = wide_to_utf8_alloc(frame_name);
        if (!frame_name_utf8) {
            fclose(file);
            set_error(error_message, error_message_count, L"Could not convert a ugoira frame name to UTF-8.");
            return 0;
        }

        snprintf(line, sizeof(line), "file '%s'\n", frame_name_utf8);
        fwrite(line, 1, strlen(line), file);
        snprintf(line, sizeof(line), "duration %.6f\n", duration);
        fwrite(line, 1, strlen(line), file);
        free(frame_name_utf8);
    }

    {
        char line[256];
        char* frame_name_utf8 = wide_to_utf8_alloc(ugoira->frames[ugoira->frame_count - 1].file);
        if (!frame_name_utf8) {
            fclose(file);
            set_error(error_message, error_message_count, L"Could not finalize the ugoira concat file.");
            return 0;
        }
        snprintf(line, sizeof(line), "file '%s'\n", frame_name_utf8);
        fwrite(line, 1, strlen(line), file);
        free(frame_name_utf8);
    }

    fclose(file);
    set_error(error_message, error_message_count, L"");
    return 1;
}

static const wchar_t* ugoira_output_extension(const wchar_t* format)
{
    if (!format || !format[0] || _wcsicmp(format, L"gif") == 0) {
        return L"gif";
    }
    if (_wcsicmp(format, L"mp4") == 0) {
        return L"mp4";
    }
    if (_wcsicmp(format, L"webm") == 0 || _wcsicmp(format, L"vp8") == 0 || _wcsicmp(format, L"vp9") == 0 || _wcsicmp(format, L"vp9-lossless") == 0) {
        return L"webm";
    }
    if (_wcsicmp(format, L"zip") == 0 || _wcsicmp(format, L"copy") == 0) {
        return L"zip";
    }
    return L"gif";
}

static int convert_ugoira_with_ffmpeg(
    const AppPaths* paths,
    const AppConfig* config,
    const wchar_t* concat_path,
    const wchar_t* working_directory,
    const wchar_t* output_path,
    wchar_t* error_message,
    size_t error_message_count
)
{
    wchar_t ffmpeg_path[PATH_BUFFER_COUNT];
    wchar_t command_line[PATH_BUFFER_COUNT * 4];
    DWORD exit_code = 0;

    if (!detect_ffmpeg_path(paths, config->ffmpeg_path, ffmpeg_path, PATH_BUFFER_COUNT)) {
        set_last_diagnosticf(
            L"FFmpeg was not found. Configured ffmpeg_path: %ls",
            config->ffmpeg_path[0] ? config->ffmpeg_path : L"(auto detect)"
        );
        set_error(error_message, error_message_count, L"FFmpeg was not found. Install FFmpeg or set ffmpeg_path first.");
        return 0;
    }

    if (_wcsicmp(config->ugoira_format, L"gif") == 0 || !config->ugoira_format[0]) {
        swprintf_s(
            command_line,
            _countof(command_line),
            L"\"%ls\" -y -hide_banner -loglevel error -f concat -safe 0 -i \"%ls\" -vf \"split[s0][s1];[s0]palettegen=stats_mode=diff[p];[s1][p]paletteuse=dither=sierra2_4a\" -loop 0 \"%ls\"",
            ffmpeg_path,
            concat_path,
            output_path
        );
    } else if (_wcsicmp(config->ugoira_format, L"mp4") == 0) {
        swprintf_s(
            command_line,
            _countof(command_line),
            L"\"%ls\" -y -hide_banner -loglevel error -f concat -safe 0 -i \"%ls\" -vsync vfr -pix_fmt yuv420p -movflags +faststart \"%ls\"",
            ffmpeg_path,
            concat_path,
            output_path
        );
    } else if (_wcsicmp(config->ugoira_format, L"vp8") == 0) {
        swprintf_s(
            command_line,
            _countof(command_line),
            L"\"%ls\" -y -hide_banner -loglevel error -f concat -safe 0 -i \"%ls\" -vsync vfr -c:v libvpx -auto-alt-ref 0 -b:v 0 -crf 10 \"%ls\"",
            ffmpeg_path,
            concat_path,
            output_path
        );
    } else if (_wcsicmp(config->ugoira_format, L"vp9-lossless") == 0) {
        swprintf_s(
            command_line,
            _countof(command_line),
            L"\"%ls\" -y -hide_banner -loglevel error -f concat -safe 0 -i \"%ls\" -vsync vfr -c:v libvpx-vp9 -lossless 1 \"%ls\"",
            ffmpeg_path,
            concat_path,
            output_path
        );
    } else {
        swprintf_s(
            command_line,
            _countof(command_line),
            L"\"%ls\" -y -hide_banner -loglevel error -f concat -safe 0 -i \"%ls\" -vsync vfr -c:v libvpx-vp9 -b:v 0 -crf 30 \"%ls\"",
            ffmpeg_path,
            concat_path,
            output_path
        );
    }

    if (!run_process_and_wait(command_line, working_directory, &exit_code, error_message, error_message_count)) {
        return 0;
    }

    set_error(error_message, error_message_count, L"");
    return 1;
}

static int pixiv_download_ugoira(
    const AppPaths* paths,
    const AppConfig* config,
    const PixivArtworkInfo* info,
    wchar_t* access_token,
    size_t access_token_count,
    const wchar_t* archive_path,
    const char* canonical_target_utf8,
    const wchar_t* base_path,
    wchar_t* result_message,
    size_t result_message_count,
    int* outcome_out
)
{
    PixivUgoiraInfo ugoira;
    wchar_t extension[16];
    wchar_t filename[512];
    wchar_t output_path[PATH_BUFFER_COUNT];
    wchar_t temp_root[PATH_BUFFER_COUNT];
    wchar_t zip_path[PATH_BUFFER_COUNT];
    wchar_t extract_path[PATH_BUFFER_COUNT];
    wchar_t concat_path[PATH_BUFFER_COUNT];
    wchar_t image_headers[1024];
    wchar_t error_message[512];

    ZeroMemory(&ugoira, sizeof(ugoira));
    temp_root[0] = L'\0';

    if (_wcsicmp(config->ugoira_format, L"skip") == 0) {
        swprintf_s(result_message, result_message_count, L"Skipped ugoira by config: %d", info->illust_id);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        return 1;
    }

    if (!pixiv_fetch_ugoira_metadata(config, access_token, access_token_count, info->illust_id, &ugoira, error_message, 512)) {
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    safe_wcs_copy(extension, _countof(extension), ugoira_output_extension(config->ugoira_format));
    expand_single_filename_format(config, info, extension, filename, 512);
    if (!filename[0]) {
        swprintf_s(filename, _countof(filename), L"%d.%ls", info->illust_id, extension);
    }

    safe_wcs_copy(output_path, _countof(output_path), base_path);
    PathAppendW(output_path, filename);
    ensure_directory_for_file(output_path);

    if (regular_file_exists(output_path)) {
        archive_append_canonical_url(archive_path, canonical_target_utf8);
        swprintf_s(result_message, result_message_count, L"Skipped existing ugoira file: %ls", output_path);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        pixiv_ugoira_info_free(&ugoira);
        return 1;
    }

    build_image_request_headers(image_headers, _countof(image_headers));

    if (_wcsicmp(extension, L"zip") == 0) {
        if (!pixiv_download_file_with_retry(config, ugoira.zip_url, image_headers, output_path, L"Pixiv ugoira ZIP download failed.", error_message, 512)) {
            pixiv_ugoira_info_free(&ugoira);
            set_error(result_message, result_message_count, error_message);
            return 0;
        }

        archive_append_canonical_url(archive_path, canonical_target_utf8);
        swprintf_s(result_message, result_message_count, L"Downloaded ugoira ZIP: %ls", output_path);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED;
        }
        pixiv_ugoira_info_free(&ugoira);
        return 1;
    }

    if (!create_unique_temp_directory(temp_root, _countof(temp_root), error_message, 512)) {
        pixiv_ugoira_info_free(&ugoira);
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    safe_wcs_copy(zip_path, _countof(zip_path), temp_root);
    PathAppendW(zip_path, L"ugoira.zip");
    if (!pixiv_download_file_with_retry(config, ugoira.zip_url, image_headers, zip_path, L"Pixiv ugoira ZIP download failed.", error_message, 512)) {
        remove_path_recursive(temp_root);
        pixiv_ugoira_info_free(&ugoira);
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    safe_wcs_copy(extract_path, _countof(extract_path), temp_root);
    PathAppendW(extract_path, L"frames");
    CreateDirectoryW(extract_path, NULL);

    if (!expand_zip_archive(zip_path, extract_path, error_message, 512)) {
        remove_path_recursive(temp_root);
        pixiv_ugoira_info_free(&ugoira);
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    if (!write_ugoira_concat_file(extract_path, &ugoira, concat_path, _countof(concat_path), error_message, 512)) {
        remove_path_recursive(temp_root);
        pixiv_ugoira_info_free(&ugoira);
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    if (!convert_ugoira_with_ffmpeg(paths, config, concat_path, extract_path, output_path, error_message, 512)) {
        remove_path_recursive(temp_root);
        pixiv_ugoira_info_free(&ugoira);
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    remove_path_recursive(temp_root);
    pixiv_ugoira_info_free(&ugoira);
    archive_append_canonical_url(archive_path, canonical_target_utf8);
    swprintf_s(result_message, result_message_count, L"Downloaded ugoira: %ls", output_path);
    if (outcome_out) {
        *outcome_out = PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED;
    }
    return 1;
}

static int write_novel_text_file(
    const wchar_t* output_path,
    const PixivNovelInfo* info,
    const wchar_t* canonical_url
)
{
    char* title_utf8 = NULL;
    char* user_name_utf8 = NULL;
    char* user_account_utf8 = NULL;
    char* canonical_utf8 = NULL;
    char* output = NULL;
    size_t capacity = 0;
    int written = 0;

    if (!output_path || !info || !info->text_utf8) {
        return 0;
    }
    title_utf8 = wide_to_utf8_alloc(info->title);
    user_name_utf8 = wide_to_utf8_alloc(info->user_name);
    user_account_utf8 = wide_to_utf8_alloc(info->user_account);
    canonical_utf8 = wide_to_utf8_alloc(canonical_url ? canonical_url : L"");

    capacity = strlen(info->text_utf8)
        + (title_utf8 ? strlen(title_utf8) : 0)
        + (user_name_utf8 ? strlen(user_name_utf8) : 0)
        + (user_account_utf8 ? strlen(user_account_utf8) : 0)
        + (canonical_utf8 ? strlen(canonical_utf8) : 0)
        + 256;
    output = (char*)calloc(capacity, sizeof(char));
    if (output) {
        output[0] = (char)0xEF;
        output[1] = (char)0xBB;
        output[2] = (char)0xBF;
        snprintf(
            output + 3,
            capacity - 3,
            "Title: %s\nAuthor: %s (@%s) [%d]\nURL: %s\n\n%s",
            title_utf8 ? title_utf8 : "",
            user_name_utf8 ? user_name_utf8 : "",
            user_account_utf8 ? user_account_utf8 : "",
            info->user_id,
            canonical_utf8 ? canonical_utf8 : "",
            info->text_utf8
        );
        written = write_text_file_utf8(output_path, output);
    }

    free(output);
    free(title_utf8);
    free(user_name_utf8);
    free(user_account_utf8);
    free(canonical_utf8);
    return written;
}

static int pixiv_download_novel_url_images(
    const AppConfig* config,
    const char* webview_json,
    const char* array_key,
    const wchar_t* filename_prefix,
    const wchar_t* output_directory,
    const wchar_t* image_headers,
    int* failed_count_out
)
{
    char* array_text = NULL;
    const char* p = NULL;
    int image_index = 0;
    int downloaded_count = 0;

    if (!webview_json || !array_key || !filename_prefix || !output_directory) {
        return 0;
    }
    array_text = json_extract_array_alloc_local(webview_json, array_key);
    if (!array_text) {
        return 0;
    }

    p = skip_ws_local(array_text);
    if (*p == '[') {
        ++p;
    }
    while (*p) {
        char url_utf8[2048];
        wchar_t* url_wide = NULL;
        wchar_t extension[32];
        wchar_t filename[256];
        wchar_t output_path[PATH_BUFFER_COUNT];
        wchar_t error_message[512];

        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '"' || !extract_json_string_local(p, url_utf8, sizeof(url_utf8))) {
            p = skip_json_value_local(p);
            if (*p == ',') {
                ++p;
            }
            continue;
        }

        ++image_index;
        url_wide = utf8_to_wide_alloc(url_utf8);
        if (url_wide && url_wide[0]) {
            if (!get_url_extension(url_wide, extension, _countof(extension))) {
                safe_wcs_copy(extension, _countof(extension), L"jpg");
            }
            swprintf_s(filename, _countof(filename), L"%ls_%02d.%ls", filename_prefix, image_index, extension);
            safe_wcs_copy(output_path, _countof(output_path), output_directory);
            PathAppendW(output_path, filename);
            if (regular_file_exists(output_path)
                || pixiv_download_file_with_retry(
                    config,
                    url_wide,
                    image_headers,
                    output_path,
                    L"Pixiv novel embedded image download failed.",
                    error_message,
                    _countof(error_message))) {
                ++downloaded_count;
            } else if (failed_count_out) {
                ++(*failed_count_out);
            }
        } else if (failed_count_out) {
            ++(*failed_count_out);
        }
        free(url_wide);

        p = skip_string_value_local(p);
        p = skip_ws_local(p);
        if (*p == ',') {
            ++p;
        }
    }

    free(array_text);
    return downloaded_count;
}

static int pixiv_download_novel_referenced_illusts(
    const AppConfig* config,
    char* webview_json,
    wchar_t* access_token,
    size_t access_token_count,
    const wchar_t* output_directory,
    const wchar_t* image_headers,
    int* failed_count_out
)
{
    char* array_text = NULL;
    const char* p = NULL;
    int downloaded_count = 0;

    array_text = json_extract_array_alloc_local(webview_json, "illusts");
    if (!array_text) {
        return 0;
    }
    p = skip_ws_local(array_text);
    if (*p == '[') {
        ++p;
    }

    while (*p) {
        char id_text[64];
        int illust_id = 0;
        PixivArtworkInfo artwork;
        wchar_t error_message[512];
        int page_index = 0;

        p = skip_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '"' || !extract_json_string_local(p, id_text, sizeof(id_text))) {
            p = skip_json_value_local(p);
            if (*p == ',') {
                ++p;
            }
            continue;
        }

        illust_id = atoi(id_text);
        ZeroMemory(&artwork, sizeof(artwork));
        if (illust_id > 0
            && pixiv_fetch_artwork_info(
                config,
                access_token,
                access_token_count,
                illust_id,
                &artwork,
                error_message,
                _countof(error_message))) {
            int url_count = artwork.page_url_count > 0 ? artwork.page_url_count : (artwork.original_image_url[0] ? 1 : 0);
            for (page_index = 0; page_index < url_count; ++page_index) {
                const wchar_t* image_url = artwork.page_url_count > 0 ? artwork.page_urls[page_index] : artwork.original_image_url;
                wchar_t extension[32];
                wchar_t filename[256];
                wchar_t output_path[PATH_BUFFER_COUNT];

                if (!image_url || !image_url[0]) {
                    continue;
                }
                if (!get_url_extension(image_url, extension, _countof(extension))) {
                    safe_wcs_copy(extension, _countof(extension), L"jpg");
                }
                swprintf_s(filename, _countof(filename), L"pixiv_%d_%02d.%ls", illust_id, page_index + 1, extension);
                safe_wcs_copy(output_path, _countof(output_path), output_directory);
                PathAppendW(output_path, filename);
                if (regular_file_exists(output_path)
                    || pixiv_download_file_with_retry(
                        config,
                        image_url,
                        image_headers,
                        output_path,
                        L"Pixiv novel referenced illustration download failed.",
                        error_message,
                        _countof(error_message))) {
                    ++downloaded_count;
                } else if (failed_count_out) {
                    ++(*failed_count_out);
                }
            }
        } else if (failed_count_out) {
            ++(*failed_count_out);
        }
        pixiv_artwork_info_free(&artwork);

        p = skip_string_value_local(p);
        p = skip_ws_local(p);
        if (*p == ',') {
            ++p;
        }
    }

    free(array_text);
    return downloaded_count;
}

static int pixiv_download_novel_id_internal(
    const AppPaths* paths,
    const AppConfig* config,
    int novel_id,
    wchar_t* reusable_access_token,
    size_t reusable_access_token_count,
    int* outcome_out,
    wchar_t* result_message,
    size_t result_message_count
)
{
    wchar_t local_access_token[2048];
    wchar_t* access_token = reusable_access_token;
    wchar_t error_message[512];
    wchar_t canonical_target[256];
    char canonical_target_utf8[256];
    wchar_t archive_path[PATH_BUFFER_COUNT];
    wchar_t download_root[PATH_BUFFER_COUNT];
    wchar_t artist_folder[512];
    wchar_t novel_folder[512];
    wchar_t output_directory[PATH_BUFFER_COUNT];
    wchar_t text_filename[512];
    wchar_t text_path[PATH_BUFFER_COUNT];
    wchar_t image_headers[1024];
    PixivNovelInfo info;
    PixivArtworkInfo format_info;
    int image_count = 0;
    int image_failure_count = 0;

    if (outcome_out) {
        *outcome_out = PIXIV_DOWNLOAD_OUTCOME_FAILED;
    }
    build_canonical_novel_url(novel_id, canonical_target, _countof(canonical_target));
    snprintf(canonical_target_utf8, sizeof(canonical_target_utf8), "https://www.pixiv.net/novel/show.php?id=%d", novel_id);
    if (!config->download_novels) {
        swprintf_s(result_message, result_message_count, L"Skipped unselected novel: %ls", canonical_target);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        return 1;
    }

    if (!access_token || !access_token[0]) {
        access_token = local_access_token;
        reusable_access_token_count = _countof(local_access_token);
        if (!pixiv_refresh_access_token(config, access_token, reusable_access_token_count, error_message, _countof(error_message))) {
            set_error(result_message, result_message_count, error_message);
            return 0;
        }
    }

    ZeroMemory(&info, sizeof(info));
    if (!pixiv_fetch_novel_info(
            config,
            access_token,
            reusable_access_token_count,
            novel_id,
            &info,
            error_message,
            _countof(error_message))) {
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    resolve_against_app_root(paths, config->archive_file, archive_path, _countof(archive_path));
    resolve_against_app_root(paths, config->download_dir, download_root, _countof(download_root));
    ZeroMemory(&format_info, sizeof(format_info));
    format_info.user_id = info.user_id;
    safe_wcs_copy(format_info.user_name, _countof(format_info.user_name), info.user_name);
    safe_wcs_copy(format_info.user_account, _countof(format_info.user_account), info.user_account);
    expand_artist_folder_format(config, &format_info, artist_folder, _countof(artist_folder));

    swprintf_s(novel_folder, _countof(novel_folder), L"%ls [%d]", info.title[0] ? info.title : L"novel", novel_id);
    make_safe_filename(novel_folder);
    safe_wcs_copy(output_directory, _countof(output_directory), download_root);
    if (artist_folder[0]) {
        PathAppendW(output_directory, artist_folder);
    }
    PathAppendW(output_directory, novel_folder);
    SHCreateDirectoryExW(NULL, output_directory, NULL);

    swprintf_s(text_filename, _countof(text_filename), L"%ls (%d).txt", info.title[0] ? info.title : L"novel", novel_id);
    make_safe_filename(text_filename);
    safe_wcs_copy(text_path, _countof(text_path), output_directory);
    PathAppendW(text_path, text_filename);

    if (!config->allow_duplicate_downloads
        && archive_contains_canonical_url(archive_path, canonical_target_utf8)
        && regular_file_exists(text_path)) {
        swprintf_s(result_message, result_message_count, L"Skipped downloaded novel: %ls", canonical_target);
        pixiv_novel_info_free(&info);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        return 1;
    }
    if (!regular_file_exists(text_path)
        && !write_novel_text_file(text_path, &info, canonical_target)) {
        pixiv_novel_info_free(&info);
        set_error(result_message, result_message_count, L"Could not write Pixiv novel TXT file.");
        return 0;
    }

    build_image_request_headers(image_headers, _countof(image_headers));
    if (info.cover_url[0]) {
        wchar_t extension[32];
        wchar_t cover_path[PATH_BUFFER_COUNT];
        if (!get_url_extension(info.cover_url, extension, _countof(extension))) {
            safe_wcs_copy(extension, _countof(extension), L"jpg");
        }
        safe_wcs_copy(cover_path, _countof(cover_path), output_directory);
        {
            wchar_t cover_filename[64];
            swprintf_s(cover_filename, _countof(cover_filename), L"cover.%ls", extension);
            PathAppendW(cover_path, cover_filename);
        }
        if (regular_file_exists(cover_path)
            || pixiv_download_file_with_retry(
                config,
                info.cover_url,
                image_headers,
                cover_path,
                L"Pixiv novel cover download failed.",
                error_message,
                _countof(error_message))) {
            ++image_count;
        } else {
            ++image_failure_count;
        }
    }
    image_count += pixiv_download_novel_url_images(
        config,
        info.webview_json,
        "images",
        L"image",
        output_directory,
        image_headers,
        &image_failure_count
    );
    image_count += pixiv_download_novel_referenced_illusts(
        config,
        info.webview_json,
        access_token,
        reusable_access_token_count,
        output_directory,
        image_headers,
        &image_failure_count
    );

    if (image_failure_count > 0) {
        swprintf_s(
            result_message,
            result_message_count,
            L"Saved novel TXT, but %d image(s) failed. Retry this target: %ls",
            image_failure_count,
            output_directory
        );
        pixiv_novel_info_free(&info);
        return 0;
    }

    archive_append_canonical_url(archive_path, canonical_target_utf8);
    swprintf_s(result_message, result_message_count, L"Saved novel TXT and %d image(s): %ls", image_count, output_directory);
    pixiv_novel_info_free(&info);
    if (outcome_out) {
        *outcome_out = PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED;
    }
    return 1;
}

static int pixiv_download_artwork_id_internal(
    const AppPaths* paths,
    const AppConfig* config,
    int illust_id,
    wchar_t* reusable_access_token,
    size_t reusable_access_token_count,
    int* outcome_out,
    wchar_t* result_message,
    size_t result_message_count
)
{
    int success = 0;
    wchar_t local_access_token[2048];
    const wchar_t* access_token = reusable_access_token;
    wchar_t error_message[512];
    PixivArtworkInfo info;
    wchar_t extension[32];
    wchar_t artist_folder[512];
    wchar_t multi_folder[512];
    wchar_t filename[512];
    wchar_t download_root[PATH_BUFFER_COUNT];
    wchar_t base_path[PATH_BUFFER_COUNT];
    wchar_t output_path[PATH_BUFFER_COUNT];
    wchar_t archive_path[PATH_BUFFER_COUNT];
    wchar_t canonical_target[256];
    wchar_t image_headers[1024];
    const wchar_t* primary_url = NULL;
    char canonical_target_utf8[256];
    int archived_target = 0;
    size_t access_token_count = 0;

    ZeroMemory(&info, sizeof(info));
    if (outcome_out) {
        *outcome_out = PIXIV_DOWNLOAD_OUTCOME_FAILED;
    }

    build_canonical_artwork_url(illust_id, canonical_target, 256);
    resolve_against_app_root(paths, config->archive_file, archive_path, PATH_BUFFER_COUNT);
    snprintf(canonical_target_utf8, sizeof(canonical_target_utf8), "https://www.pixiv.net/artworks/%d", illust_id);
    archived_target = !config->allow_duplicate_downloads && archive_contains_canonical_url(archive_path, canonical_target_utf8);

    if (!access_token || !access_token[0]) {
        if (reusable_access_token && reusable_access_token_count > 0) {
            if (!pixiv_refresh_access_token(config, reusable_access_token, reusable_access_token_count, error_message, 512)) {
                if (!g_pixiv_last_diagnostic[0]) {
                    set_last_diagnosticf(L"Could not refresh Pixiv access token for artwork %d. %ls", illust_id, error_message);
                }
                set_error(result_message, result_message_count, error_message);
                return 0;
            }
            access_token = reusable_access_token;
            access_token_count = reusable_access_token_count;
        } else {
            if (!pixiv_refresh_access_token(config, local_access_token, 2048, error_message, 512)) {
                if (!g_pixiv_last_diagnostic[0]) {
                    set_last_diagnosticf(L"Could not refresh Pixiv access token for artwork %d. %ls", illust_id, error_message);
                }
                set_error(result_message, result_message_count, error_message);
                return 0;
            }
            access_token = local_access_token;
            access_token_count = _countof(local_access_token);
        }
    } else if (access_token == reusable_access_token && reusable_access_token_count > 0) {
        access_token_count = reusable_access_token_count;
    } else {
        access_token_count = _countof(local_access_token);
    }

    if (!pixiv_fetch_artwork_info(config, (wchar_t*)access_token, access_token_count, illust_id, &info, error_message, 512)) {
        if (!g_pixiv_last_diagnostic[0]) {
            set_last_diagnosticf(L"Could not fetch Pixiv artwork info for illust_id=%d. %ls", illust_id, error_message);
        }
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    if ((_wcsicmp(info.illust_type, L"manga") == 0 && !config->download_manga)
        || (_wcsicmp(info.illust_type, L"manga") != 0 && !config->download_illustrations)) {
        swprintf_s(result_message, result_message_count, L"Skipped unselected %ls: %ls", info.illust_type, canonical_target);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        success = 1;
        goto cleanup;
    }

    resolve_against_app_root(paths, config->download_dir, download_root, PATH_BUFFER_COUNT);
    expand_artist_folder_format(config, &info, artist_folder, 512);
    build_image_request_headers(image_headers, 1024);

    safe_wcs_copy(base_path, PATH_BUFFER_COUNT, download_root);
    if (artist_folder[0]) {
        PathAppendW(base_path, artist_folder);
    }

    if (archived_target && pixiv_artwork_output_exists(config, &info, base_path)) {
        swprintf_s(result_message, result_message_count, L"\uB2E4\uC6B4\uBC1B\uC740 \uB9C1\uD06C\uB294 \uC911\uBCF5\uC785\uB2C8\uB2E4. \uC2A4\uD0B5\uD588\uC2B5\uB2C8\uB2E4: %ls", canonical_target);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        success = 1;
        goto cleanup;
    }

    if (_wcsicmp(info.illust_type, L"ugoira") == 0) {
        success = pixiv_download_ugoira(
            paths,
            config,
            &info,
            (wchar_t*)access_token,
            access_token_count,
            archive_path,
            canonical_target_utf8,
            base_path,
            result_message,
            result_message_count,
            outcome_out
        );
        goto cleanup;
    }

    if (info.page_count <= 1) {
        primary_url = info.original_image_url[0] ? info.original_image_url : NULL;
        if ((!primary_url || !primary_url[0]) && info.page_url_count > 0) {
            primary_url = info.page_urls[0];
        }
        if (!primary_url || !primary_url[0]) {
            set_last_diagnosticf(L"Could not find the original image URL for illust_id=%d.", illust_id);
            set_error(result_message, result_message_count, L"Could not find original image URL.");
            goto cleanup;
        }
        if (!get_url_extension(primary_url, extension, 32)) {
            safe_wcs_copy(extension, 32, L"jpg");
        }

        expand_single_filename_format(config, &info, extension, filename, 512);
        if (!filename[0]) {
            swprintf_s(filename, 512, L"%d.%ls", info.illust_id, extension);
        }

        safe_wcs_copy(output_path, PATH_BUFFER_COUNT, base_path);
        PathAppendW(output_path, filename);
        ensure_directory_for_file(output_path);

        if (regular_file_exists(output_path)) {
            archive_append_canonical_url(archive_path, canonical_target_utf8);
            swprintf_s(result_message, result_message_count, L"Skipped existing file: %ls", output_path);
            if (outcome_out) {
                *outcome_out = PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
            }
            success = 1;
            goto cleanup;
        }

        if (!pixiv_download_file_with_retry(config, primary_url, image_headers, output_path, L"Pixiv single-image download failed.", error_message, 512)) {
            set_nested_http_diagnostic(L"Pixiv single-image download failed.", error_message);
            set_error(result_message, result_message_count, error_message);
            goto cleanup;
        }

        archive_append_canonical_url(archive_path, canonical_target_utf8);
        swprintf_s(result_message, result_message_count, L"Downloaded: %ls", output_path);
        if (outcome_out) {
            *outcome_out = PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED;
        }
        success = 1;
        goto cleanup;
    }

    if (info.page_url_count <= 0) {
        set_last_diagnosticf(L"Could not parse Pixiv multi-page image URLs for illust_id=%d.", illust_id);
        set_error(result_message, result_message_count, L"Could not parse Pixiv multi-page image URLs.");
        goto cleanup;
    }

    expand_multi_folder_format(config, &info, multi_folder, 512);
    if (multi_folder[0]) {
        PathAppendW(base_path, multi_folder);
    }

    {
        int page_index = 0;
        int downloaded_count = 0;
        int skipped_count = 0;

        for (page_index = 0; page_index < info.page_url_count; ++page_index) {
            const wchar_t* page_url = info.page_urls[page_index];
            int page_number = page_index + 1;

            if (!page_url[0]) {
                continue;
            }
            if (!get_url_extension(page_url, extension, 32)) {
                safe_wcs_copy(extension, 32, L"jpg");
            }

            expand_multi_filename_format(config, &info, extension, page_number, filename, 512);
            if (!filename[0]) {
                swprintf_s(filename, 512, L"%d_%02d.%ls", info.illust_id, page_number, extension);
            }

            safe_wcs_copy(output_path, PATH_BUFFER_COUNT, base_path);
            PathAppendW(output_path, filename);
            ensure_directory_for_file(output_path);

            if (regular_file_exists(output_path)) {
                ++skipped_count;
                continue;
            }

            if (!pixiv_download_file_with_retry(config, page_url, image_headers, output_path, L"Pixiv multi-page image download failed.", error_message, 512)) {
                set_nested_http_diagnostic(L"Pixiv multi-page image download failed.", error_message);
                swprintf_s(result_message, result_message_count, L"Page %d failed: %ls", page_number, error_message);
                goto cleanup;
            }
            ++downloaded_count;
        }

        if (downloaded_count == 0 && skipped_count == 0) {
            set_error(result_message, result_message_count, L"Could not download any multi-page images.");
            goto cleanup;
        }

        archive_append_canonical_url(archive_path, canonical_target_utf8);
        swprintf_s(
            result_message,
            result_message_count,
            L"Downloaded %d page(s), skipped %d existing file(s): %ls",
            downloaded_count,
            skipped_count,
            base_path
        );
        if (outcome_out) {
            *outcome_out = downloaded_count > 0 ? PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED : PIXIV_DOWNLOAD_OUTCOME_SKIPPED;
        }
        success = 1;
    }

cleanup:
    pixiv_artwork_info_free(&info);
    return success;
}

static int pixiv_download_profile_page_target(
    const AppPaths* paths,
    const AppConfig* config,
    int user_id,
    int page_number,
    PixivProfilePageTargetType page_target_type,
    wchar_t* reusable_access_token,
    size_t reusable_access_token_count,
    wchar_t* result_message,
    size_t result_message_count
)
{
    int* illust_ids = NULL;
    int illust_id_count = 0;
    int index = 0;
    int downloaded_count = 0;
    int skipped_count = 0;
    int failure_count = 0;
    wchar_t local_access_token[2048];
    wchar_t* shared_access_token = reusable_access_token;
    wchar_t error_message[512];
    wchar_t single_result[2048];
    wchar_t first_failure[1024];
    wchar_t first_failure_detail[2048];
    int download_all_pages = page_number <= 0 ? 1 : 0;

    if (!shared_access_token || reusable_access_token_count == 0) {
        shared_access_token = local_access_token;
        reusable_access_token_count = _countof(local_access_token);
        shared_access_token[0] = L'\0';
    }
    first_failure[0] = L'\0';
    first_failure_detail[0] = L'\0';

    if (!shared_access_token[0]
        && !pixiv_refresh_access_token(config, shared_access_token, reusable_access_token_count, error_message, _countof(error_message))) {
        if (!g_pixiv_last_diagnostic[0]) {
            set_last_diagnosticf(L"Could not refresh Pixiv access token for user %d page target. %ls", user_id, error_message);
        }
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    if (!pixiv_build_profile_page_id_list(
            config,
            shared_access_token,
            reusable_access_token_count,
            user_id,
            page_target_type,
            page_number,
            &illust_ids,
            &illust_id_count,
            error_message,
            512)) {
        set_error(result_message, result_message_count, error_message);
        return 0;
    }

    for (index = 0; index < illust_id_count; ++index) {
        int outcome = PIXIV_DOWNLOAD_OUTCOME_FAILED;

        int item_succeeded = 0;

        if (page_target_type == PIXIV_PROFILE_PAGE_TARGET_NOVELS) {
            item_succeeded = pixiv_download_novel_id_internal(
                paths,
                config,
                illust_ids[index],
                shared_access_token,
                reusable_access_token_count,
                &outcome,
                single_result,
                2048
            );
        } else {
            item_succeeded = pixiv_download_artwork_id_internal(
                paths,
                config,
                illust_ids[index],
                shared_access_token,
                reusable_access_token_count,
                &outcome,
                single_result,
                2048
            );
        }

        if (item_succeeded) {
            if (outcome == PIXIV_DOWNLOAD_OUTCOME_DOWNLOADED) {
                ++downloaded_count;
            } else {
                ++skipped_count;
            }
        } else {
            ++failure_count;
            if (!first_failure[0]) {
                safe_wcs_copy(first_failure, 1024, single_result);
                pixiv_get_last_diagnostic(first_failure_detail, _countof(first_failure_detail));
            }
        }
        if (index + 1 < illust_id_count) {
            pixiv_sleep_between_requests(config);
        }
    }

    free(illust_ids);

    if (failure_count > 0) {
        if (download_all_pages) {
            swprintf_s(
                result_message,
                result_message_count,
                L"User %d all %ls finished with %d failure(s): %d downloaded, %d skipped. First error: %ls",
                user_id,
                pixiv_profile_page_target_type_name(page_target_type),
                failure_count,
                downloaded_count,
                skipped_count,
                first_failure
            );
        } else {
            swprintf_s(
                result_message,
                result_message_count,
                L"User %d %ls page %d finished with %d failure(s): %d downloaded, %d skipped. First error: %ls",
                user_id,
                pixiv_profile_page_target_type_name(page_target_type),
                page_number,
                failure_count,
                downloaded_count,
                skipped_count,
                first_failure
            );
        }
        if (first_failure_detail[0]) {
            set_last_diagnostic_text(first_failure_detail);
        } else if (!g_pixiv_last_diagnostic[0]) {
            set_last_diagnosticf(
                L"User %d %ls target finished with %d failure(s). First error: %ls",
                user_id,
                pixiv_profile_page_target_type_name(page_target_type),
                failure_count,
                first_failure[0] ? first_failure : L"(unknown)"
            );
        }
        return 0;
    }

    if (download_all_pages) {
        swprintf_s(
            result_message,
            result_message_count,
            L"User %d all %ls complete: %d downloaded, %d skipped.",
            user_id,
            pixiv_profile_page_target_type_name(page_target_type),
            downloaded_count,
            skipped_count
        );
    } else {
        swprintf_s(
            result_message,
            result_message_count,
            L"User %d %ls page %d complete: %d downloaded, %d skipped.",
            user_id,
            pixiv_profile_page_target_type_name(page_target_type),
            page_number,
            downloaded_count,
            skipped_count
        );
    }
    return 1;
}

int pixiv_download_artwork_with_access_token(
    const AppPaths* paths,
    const AppConfig* config,
    const wchar_t* target,
    wchar_t* reusable_access_token,
    size_t reusable_access_token_count,
    wchar_t* result_message,
    size_t result_message_count
)
{
    int illust_id = 0;
    int novel_id = 0;
    int user_id = 0;
    int page_number = 1;
    PixivProfilePageTargetType page_target_type = PIXIV_PROFILE_PAGE_TARGET_NONE;

    pixiv_clear_last_diagnostic();

    if (pixiv_extract_novel_id(target, &novel_id)) {
        return pixiv_download_novel_id_internal(
            paths,
            config,
            novel_id,
            reusable_access_token,
            reusable_access_token_count,
            NULL,
            result_message,
            result_message_count
        );
    }

    if (pixiv_parse_user_artworks_page_target(target, &user_id, &page_number, &page_target_type)) {
        return pixiv_download_profile_page_target(
            paths,
            config,
            user_id,
            page_number,
            page_target_type,
            reusable_access_token,
            reusable_access_token_count,
            result_message,
            result_message_count
        );
    }

    if (pixiv_parse_user_profile_target(target, &user_id)) {
        return pixiv_download_profile_page_target(
            paths,
            config,
            user_id,
            0,
            PIXIV_PROFILE_PAGE_TARGET_ILLUSTRATIONS,
            reusable_access_token,
            reusable_access_token_count,
            result_message,
            result_message_count
        );
    }

    if (!pixiv_extract_artwork_id(target, &illust_id)) {
        set_last_diagnosticf(L"Could not parse a Pixiv artwork ID from target: %ls", target ? target : L"(empty)");
        set_error(result_message, result_message_count, L"Could not parse Pixiv artwork ID.");
        return 0;
    }

    return pixiv_download_artwork_id_internal(
        paths,
        config,
        illust_id,
        reusable_access_token,
        reusable_access_token_count,
        NULL,
        result_message,
        result_message_count
    );
}

int pixiv_download_artwork(
    const AppPaths* paths,
    const AppConfig* config,
    const wchar_t* target,
    wchar_t* result_message,
    size_t result_message_count
)
{
    return pixiv_download_artwork_with_access_token(
        paths,
        config,
        target,
        NULL,
        0,
        result_message,
        result_message_count
    );
}
