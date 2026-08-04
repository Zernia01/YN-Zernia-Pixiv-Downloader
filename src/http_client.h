#pragma once

#include <stddef.h>
#include <windows.h>

typedef struct HttpResponse {
    DWORD status_code;
    char* body;
    size_t body_size;
} HttpResponse;

void http_set_detailed_errors_enabled(int enabled);
void http_clear_last_diagnostic(void);
int http_get_last_diagnostic(wchar_t* output, size_t output_count);
void http_response_free(HttpResponse* response);
int http_request_utf8(
    const wchar_t* method,
    const wchar_t* url,
    const wchar_t* extra_headers,
    const char* request_body,
    DWORD request_body_size,
    HttpResponse* response,
    wchar_t* error_message,
    size_t error_message_count
);
int http_download_to_file(
    const wchar_t* url,
    const wchar_t* extra_headers,
    const wchar_t* output_path,
    wchar_t* error_message,
    size_t error_message_count
);
