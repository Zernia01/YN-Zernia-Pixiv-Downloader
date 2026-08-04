#pragma once

#include "app_paths.h"
#include "config.h"

typedef struct PixivArtworkInfo {
    int illust_id;
    int user_id;
    int page_count;
    int page_url_count;
    wchar_t title[256];
    wchar_t illust_type[32];
    wchar_t original_image_url[1024];
    wchar_t user_name[128];
    wchar_t user_account[128];
    wchar_t (*page_urls)[1024];
} PixivArtworkInfo;

void pixiv_set_detailed_logging(int enabled);
void pixiv_clear_last_diagnostic(void);
int pixiv_get_last_diagnostic(wchar_t* output, size_t output_count);
void pixiv_sleep_between_requests(const AppConfig* config);
int pixiv_extract_artwork_id(const wchar_t* target, int* illust_id_out);
void pixiv_artwork_info_free(PixivArtworkInfo* info);
int pixiv_build_pkce_login_url(
    wchar_t* code_verifier,
    size_t code_verifier_count,
    wchar_t* login_url,
    size_t login_url_count,
    wchar_t* error_message,
    size_t error_message_count
);
int pixiv_exchange_auth_code_for_refresh_token(
    const wchar_t* auth_code,
    const wchar_t* code_verifier,
    wchar_t* refresh_token,
    size_t refresh_token_count,
    wchar_t* error_message,
    size_t error_message_count
);
int pixiv_prepare_access_token(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    wchar_t* error_message,
    size_t error_message_count
);
int pixiv_expand_target_to_artwork_urls(
    const AppConfig* config,
    wchar_t* access_token,
    size_t access_token_count,
    const wchar_t* target,
    wchar_t** urls_text_out,
    int* url_count_out,
    wchar_t* error_message,
    size_t error_message_count
);
int pixiv_download_artwork(
    const AppPaths* paths,
    const AppConfig* config,
    const wchar_t* target,
    wchar_t* result_message,
    size_t result_message_count
);
int pixiv_download_artwork_with_access_token(
    const AppPaths* paths,
    const AppConfig* config,
    const wchar_t* target,
    wchar_t* reusable_access_token,
    size_t reusable_access_token_count,
    wchar_t* result_message,
    size_t result_message_count
);
