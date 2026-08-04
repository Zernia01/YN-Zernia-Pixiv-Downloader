#include "http_client.h"

#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winhttp.h>

#define HTTP_DIAGNOSTIC_BUFFER_COUNT 4096

static int g_http_detailed_errors_enabled = 0;
static __declspec(thread) wchar_t g_http_last_diagnostic[HTTP_DIAGNOSTIC_BUFFER_COUNT];

static void sanitize_text_for_log(wchar_t* text)
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

static void trim_trailing_whitespace(wchar_t* text)
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

static void set_last_diagnostic(const wchar_t* text)
{
    if (!g_http_detailed_errors_enabled) {
        return;
    }
    safe_wcs_copy(g_http_last_diagnostic, HTTP_DIAGNOSTIC_BUFFER_COUNT, text ? text : L"");
}

static void format_system_error_message(DWORD error_code, wchar_t* buffer, size_t buffer_count)
{
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = 0;

    if (!buffer || buffer_count == 0) {
        return;
    }

    buffer[0] = L'\0';
    length = FormatMessageW(flags, NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, (DWORD)buffer_count, NULL);
    if (length == 0) {
        swprintf_s(buffer, buffer_count, L"Unknown system error.");
        return;
    }

    sanitize_text_for_log(buffer);
    trim_trailing_whitespace(buffer);
}

static void set_last_winhttp_error(const wchar_t* context, DWORD error_code, const wchar_t* url)
{
    wchar_t system_message[256];
    wchar_t combined[HTTP_DIAGNOSTIC_BUFFER_COUNT];

    if (!g_http_detailed_errors_enabled) {
        return;
    }

    format_system_error_message(error_code, system_message, _countof(system_message));
    if (url && url[0]) {
        swprintf_s(
            combined,
            _countof(combined),
            L"%ls | WinHTTP/GetLastError=%lu (%ls) | URL: %ls",
            context ? context : L"HTTP request failed.",
            (unsigned long)error_code,
            system_message,
            url
        );
    } else {
        swprintf_s(
            combined,
            _countof(combined),
            L"%ls | WinHTTP/GetLastError=%lu (%ls)",
            context ? context : L"HTTP request failed.",
            (unsigned long)error_code,
            system_message
        );
    }
    set_last_diagnostic(combined);
}

static void set_last_http_status_error(const wchar_t* context, const wchar_t* url, const HttpResponse* response)
{
    wchar_t combined[HTTP_DIAGNOSTIC_BUFFER_COUNT];
    char snippet_utf8[256];
    wchar_t* snippet_wide = NULL;
    size_t snippet_length = 0;

    if (!g_http_detailed_errors_enabled || !response) {
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
            sanitize_text_for_log(snippet_wide);
            trim_trailing_whitespace(snippet_wide);
        }
    }

    if (url && url[0] && snippet_wide && snippet_wide[0]) {
        swprintf_s(
            combined,
            _countof(combined),
            L"%ls | HTTP %lu | URL: %ls | Response: %ls",
            context ? context : L"HTTP request returned an error.",
            (unsigned long)response->status_code,
            url,
            snippet_wide
        );
    } else if (url && url[0]) {
        swprintf_s(
            combined,
            _countof(combined),
            L"%ls | HTTP %lu | URL: %ls",
            context ? context : L"HTTP request returned an error.",
            (unsigned long)response->status_code,
            url
        );
    } else if (snippet_wide && snippet_wide[0]) {
        swprintf_s(
            combined,
            _countof(combined),
            L"%ls | HTTP %lu | Response: %ls",
            context ? context : L"HTTP request returned an error.",
            (unsigned long)response->status_code,
            snippet_wide
        );
    } else {
        swprintf_s(
            combined,
            _countof(combined),
            L"%ls | HTTP %lu",
            context ? context : L"HTTP request returned an error.",
            (unsigned long)response->status_code
        );
    }

    free(snippet_wide);
    set_last_diagnostic(combined);
}

void http_set_detailed_errors_enabled(int enabled)
{
    g_http_detailed_errors_enabled = enabled ? 1 : 0;
    if (!g_http_detailed_errors_enabled) {
        g_http_last_diagnostic[0] = L'\0';
    }
}

void http_clear_last_diagnostic(void)
{
    g_http_last_diagnostic[0] = L'\0';
}

int http_get_last_diagnostic(wchar_t* output, size_t output_count)
{
    if (!output || output_count == 0) {
        return 0;
    }
    output[0] = L'\0';
    if (!g_http_last_diagnostic[0]) {
        return 0;
    }
    safe_wcs_copy(output, output_count, g_http_last_diagnostic);
    return 1;
}

static void set_error(wchar_t* error_message, size_t error_message_count, const wchar_t* text)
{
    if (!error_message || error_message_count == 0) {
        return;
    }
    safe_wcs_copy(error_message, error_message_count, text);
}

static HINTERNET open_request_handles(
    const wchar_t* url,
    const wchar_t* method,
    HINTERNET* session_out,
    HINTERNET* connect_out,
    wchar_t* object_path,
    size_t object_path_count,
    wchar_t* host_name,
    size_t host_name_count,
    INTERNET_PORT* port_out,
    DWORD* request_flags_out
)
{
    URL_COMPONENTS components;
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;
    wchar_t path_buffer[2048];
    wchar_t host_buffer[512];

    ZeroMemory(&components, sizeof(components));
    ZeroMemory(path_buffer, sizeof(path_buffer));
    ZeroMemory(host_buffer, sizeof(host_buffer));

    components.dwStructSize = sizeof(components);
    components.lpszHostName = host_buffer;
    components.dwHostNameLength = (DWORD)(sizeof(host_buffer) / sizeof(host_buffer[0]));
    components.lpszUrlPath = path_buffer;
    components.dwUrlPathLength = (DWORD)(sizeof(path_buffer) / sizeof(path_buffer[0]));
    components.lpszExtraInfo = path_buffer + 1024;
    components.dwExtraInfoLength = 1024;

    if (!WinHttpCrackUrl(url, 0, 0, &components)) {
        set_last_winhttp_error(L"WinHttpCrackUrl failed.", GetLastError(), url);
        return NULL;
    }

    safe_wcs_copy(host_name, host_name_count, components.lpszHostName);
    safe_wcs_copy(object_path, object_path_count, components.lpszUrlPath);
    if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo) {
        wcscat_s(object_path, object_path_count, components.lpszExtraInfo);
    }

    *port_out = components.nPort;
    *request_flags_out = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;

    session = WinHttpOpen(L"YN Zernia Pixiv Downloader 6.1/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        set_last_winhttp_error(L"WinHttpOpen failed.", GetLastError(), url);
        return NULL;
    }
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 60000);

    connect = WinHttpConnect(session, host_name, *port_out, 0);
    if (!connect) {
        set_last_winhttp_error(L"WinHttpConnect failed.", GetLastError(), url);
        WinHttpCloseHandle(session);
        return NULL;
    }

    request = WinHttpOpenRequest(connect, method, object_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, *request_flags_out);
    if (!request) {
        set_last_winhttp_error(L"WinHttpOpenRequest failed.", GetLastError(), url);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return NULL;
    }

    *session_out = session;
    *connect_out = connect;
    return request;
}

static int read_response_to_memory(HINTERNET request, HttpResponse* response)
{
    DWORD available = 0;
    DWORD status_size = sizeof(DWORD);
    char* buffer = NULL;
    size_t used = 0;

    if (!response) {
        return 0;
    }

    ZeroMemory(response, sizeof(*response));

    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &response->status_code, &status_size, NULL)) {
        response->status_code = 0;
    }

    for (;;) {
        char chunk[8192];
        DWORD downloaded = 0;
        DWORD to_read = 0;

        if (!WinHttpQueryDataAvailable(request, &available)) {
            set_last_winhttp_error(L"WinHttpQueryDataAvailable failed.", GetLastError(), NULL);
            free(buffer);
            return 0;
        }
        if (available == 0) {
            break;
        }

        to_read = available < (DWORD)sizeof(chunk) ? available : (DWORD)sizeof(chunk);
        if (!WinHttpReadData(request, chunk, to_read, &downloaded)) {
            set_last_winhttp_error(L"WinHttpReadData failed.", GetLastError(), NULL);
            free(buffer);
            return 0;
        }
        if (downloaded == 0) {
            break;
        }

        {
            char* grown = (char*)realloc(buffer, used + downloaded + 1);
            if (!grown) {
                free(buffer);
                return 0;
            }
            buffer = grown;
            memcpy(buffer + used, chunk, downloaded);
            used += downloaded;
            buffer[used] = '\0';
        }
    }

    response->body = buffer;
    response->body_size = used;
    return 1;
}

void http_response_free(HttpResponse* response)
{
    if (!response) {
        return;
    }
    free(response->body);
    response->body = NULL;
    response->body_size = 0;
    response->status_code = 0;
}

int http_request_utf8(
    const wchar_t* method,
    const wchar_t* url,
    const wchar_t* extra_headers,
    const char* request_body,
    DWORD request_body_size,
    HttpResponse* response,
    wchar_t* error_message,
    size_t error_message_count
)
{
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;
    wchar_t object_path[2048];
    wchar_t host_name[512];
    INTERNET_PORT port = 0;
    DWORD request_flags = 0;
    BOOL ok = FALSE;

    http_clear_last_diagnostic();
    request = open_request_handles(url, method, &session, &connect, object_path, 2048, host_name, 512, &port, &request_flags);
    if (!request) {
        set_error(error_message, error_message_count, L"Could not open HTTP request.");
        return 0;
    }

    ok = WinHttpSendRequest(
        request,
        extra_headers && extra_headers[0] ? extra_headers : WINHTTP_NO_ADDITIONAL_HEADERS,
        extra_headers && extra_headers[0] ? (DWORD)-1L : 0,
        request_body ? (LPVOID)request_body : WINHTTP_NO_REQUEST_DATA,
        request_body ? request_body_size : 0,
        request_body ? request_body_size : 0,
        0
    );
    if (!ok) {
        set_last_winhttp_error(L"WinHttpSendRequest failed.", GetLastError(), url);
        set_error(error_message, error_message_count, L"HTTP request failed.");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }
    if (!WinHttpReceiveResponse(request, NULL)) {
        set_last_winhttp_error(L"WinHttpReceiveResponse failed.", GetLastError(), url);
        set_error(error_message, error_message_count, L"HTTP request failed.");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }

    if (!read_response_to_memory(request, response)) {
        set_error(error_message, error_message_count, L"Could not read HTTP response.");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    set_error(error_message, error_message_count, L"");
    return 1;
}

int http_download_to_file(
    const wchar_t* url,
    const wchar_t* extra_headers,
    const wchar_t* output_path,
    wchar_t* error_message,
    size_t error_message_count
)
{
    HttpResponse response;
    FILE* file = NULL;

    ZeroMemory(&response, sizeof(response));
    if (!http_request_utf8(L"GET", url, extra_headers, NULL, 0, &response, error_message, error_message_count)) {
        return 0;
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        set_last_http_status_error(L"Image download returned an HTTP error.", url, &response);
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Image download returned an HTTP error.");
        return 0;
    }

    if (_wfopen_s(&file, output_path, L"wb") != 0 || !file) {
        http_response_free(&response);
        set_error(error_message, error_message_count, L"Could not create output image file.");
        return 0;
    }

    if (response.body && response.body_size > 0) {
        fwrite(response.body, 1, response.body_size, file);
    }
    fclose(file);
    http_response_free(&response);
    set_error(error_message, error_message_count, L"");
    return 1;
}
