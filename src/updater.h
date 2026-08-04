#pragma once

#include <stddef.h>
#include <windows.h>

#define YN_APP_VERSION L"6.1.0"

typedef struct UpdateManifest {
    wchar_t version[64];
    wchar_t download_url[2048];
    wchar_t sha256[65];
} UpdateManifest;

int updater_check_github_release(
    const wchar_t* repository,
    const wchar_t* current_version,
    UpdateManifest* manifest_out,
    int* update_available_out,
    wchar_t* error_message,
    size_t error_message_count
);

int updater_download_and_verify(
    const UpdateManifest* manifest,
    wchar_t* staged_path,
    size_t staged_path_count,
    wchar_t* error_message,
    size_t error_message_count
);

int updater_launch_apply(
    const wchar_t* staged_path,
    DWORD parent_process_id,
    const wchar_t* target_path,
    wchar_t* error_message,
    size_t error_message_count
);

int updater_apply_staged_update(DWORD parent_process_id, const wchar_t* target_path);
