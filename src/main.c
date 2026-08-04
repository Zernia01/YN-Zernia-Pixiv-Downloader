#include "app_paths.h"
#include "config.h"
#include "json_utils.h"
#include "locale.h"
#include "pixiv_api.h"
#include "resource.h"
#include "updater.h"
#include "utf8.h"

#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>
#include <wchar.h>
#include <wctype.h>
#include <windowsx.h>

#define WINDOW_CLASS_NAME L"YnZerniaPixivDownloaderC"
#define SETTINGS_WINDOW_CLASS_NAME L"YnZerniaPixivDownloaderSettings"
#define APP_TITLE L"YN Zernia Pixiv Downloader 6.1"
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 960
#define SETTINGS_WINDOW_WIDTH 670
#define SETTINGS_WINDOW_HEIGHT 790
#define UI_FONT_NAME L"Malgun Gothic"
#define PANEL_RADIUS 14
#define MAIN_CONTENT_LEFT 24
#define MAIN_CONTENT_RIGHT 1256
#define MAIN_CONTENT_WIDTH (MAIN_CONTENT_RIGHT - MAIN_CONTENT_LEFT)
#define LARGE_EDIT_TEXT_LIMIT (64u * 1024u * 1024u)
#define DOWNLOAD_TARGET_ATTEMPTS 3

#define COLOR_APP_BG RGB(244, 247, 251)
#define COLOR_PANEL_BG RGB(255, 255, 255)
#define COLOR_PANEL_SHADOW RGB(231, 237, 245)
#define COLOR_PANEL_BORDER RGB(212, 221, 233)
#define COLOR_PANEL_ACCENT RGB(218, 233, 244)
#define COLOR_INPUT_BG RGB(255, 255, 255)
#define COLOR_INPUT_READONLY_BG RGB(248, 251, 255)
#define COLOR_LOG_BG RGB(252, 253, 255)
#define COLOR_ACCENT RGB(22, 123, 221)
#define COLOR_ACCENT_DARK RGB(14, 95, 180)
#define COLOR_TEXT RGB(45, 53, 66)
#define COLOR_MUTED_TEXT RGB(114, 124, 138)
#define COLOR_BUTTON_NEUTRAL RGB(244, 247, 251)
#define COLOR_BUTTON_NEUTRAL_DARK RGB(232, 238, 246)
#define COLOR_DANGER RGB(211, 87, 79)
#define COLOR_DANGER_DARK RGB(175, 62, 55)

#define IDC_EDIT_TOKEN 1001
#define IDC_EDIT_FFMPEG 1002
#define IDC_EDIT_FFMPEG_STATUS 1003
#define IDC_COMBO_UGOIRA 1004
#define IDC_EDIT_SLEEP 1005
#define IDC_EDIT_PARALLEL 1006
#define IDC_COMBO_LANGUAGE 1007
#define IDC_CHECK_METADATA 1008
#define IDC_EDIT_ARTIST_FOLDER 1009
#define IDC_EDIT_MULTI_FOLDER 1010
#define IDC_EDIT_SINGLE_FILE 1011
#define IDC_EDIT_MULTI_FILE 1012
#define IDC_EDIT_DOWNLOAD_DIR 1013
#define IDC_EDIT_ARCHIVE_FILE 1014
#define IDC_BUTTON_SAVE 1015
#define IDC_BUTTON_RELOAD 1016
#define IDC_BUTTON_AUTH 1017
#define IDC_EDIT_TARGETS 1018
#define IDC_EDIT_INPUT_FILE 1019
#define IDC_CHECK_DRY_RUN 1020
#define IDC_CHECK_VERBOSE 1021
#define IDC_CHECK_PRINT_COMMAND 1022
#define IDC_BUTTON_START 1023
#define IDC_BUTTON_SHOW_CONFIG 1024
#define IDC_BUTTON_OPEN_DOWNLOADS 1025
#define IDC_BUTTON_README 1026
#define IDC_EDIT_LOG 1027
#define IDC_BUTTON_CLEAR_LOG 1028
#define IDC_BUTTON_AUTO_CLIPBOARD 1029
#define IDC_PROGRESS_DOWNLOAD 1030
#define IDC_BUTTON_DUPLICATE_DOWNLOAD 1031
#define IDC_BUTTON_ADD_TARGET 1032
#define IDC_BUTTON_DELETE_TARGET 1033
#define IDC_BUTTON_VIEW_TARGETS 1034
#define IDC_BUTTON_VIEW_ARCHIVE 1035
#define IDC_BUTTON_DELETE_ALL 1036
#define IDC_BUTTON_DOWNLOAD_FFMPEG 1037
#define IDC_BUTTON_INSTALL_FFMPEG 1038
#define IDC_EDIT_PROFILE_PAGE_SIZE 1039
#define IDC_BUTTON_SUPPORT 1040
#define IDC_BUTTON_STOP 1041
#define IDC_SLIDER_OPACITY 1042
#define IDC_CHECK_ILLUSTRATIONS 1043
#define IDC_CHECK_MANGA 1044
#define IDC_CHECK_NOVELS 1045
#define IDC_CHECK_AUTO_UPDATE 1046
#define IDC_EDIT_UPDATE_MANIFEST 1047
#define IDC_BUTTON_CHECK_UPDATE 1048

#define LIST_VIEW_NONE 0
#define LIST_VIEW_TARGETS 1
#define LIST_VIEW_ARCHIVE 2

#define WM_APP_DOWNLOAD_RESULT   (WM_APP + 101)
#define WM_APP_DOWNLOAD_FINISHED (WM_APP + 102)
#define WM_APP_DOWNLOAD_PREPARED (WM_APP + 103)
#define WM_APP_UPDATE_CHECKED    (WM_APP + 104)
#define WM_APP_UPDATE_DOWNLOADED (WM_APP + 105)
#define MIN_PARALLEL_DOWNLOADS 2
#define MAX_PARALLEL_DOWNLOADS 5

typedef struct DownloadWorkerContext {
    HWND window;
    AppPaths paths;
    AppConfig config;
    wchar_t* targets_text;
    wchar_t shared_access_token[2048];
    int verbose_enabled;
    HANDLE cancel_event;
} DownloadWorkerContext;

typedef struct ParallelDownloadContext {
    DownloadWorkerContext* owner;
    wchar_t** targets;
    int target_count;
    volatile LONG next_target_index;
    volatile LONG completed_count;
    volatile LONG success_count;
    volatile LONG failure_count;
} ParallelDownloadContext;

typedef struct DownloadResultMessage {
    int total_count;
    int succeeded;
    wchar_t target[2048];
    wchar_t result_message[2048];
    wchar_t detail_message[4096];
} DownloadResultMessage;

typedef struct DownloadFinishedMessage {
    int total_count;
    int success_count;
    int failure_count;
    int cancelled;
} DownloadFinishedMessage;

typedef struct DownloadPreparedMessage {
    int prepared_count;
    int failed;
    wchar_t* expanded_targets_text;
    wchar_t error_message[512];
    wchar_t detail_message[4096];
} DownloadPreparedMessage;

typedef struct UpdateCheckContext {
    HWND window;
    int manual;
    wchar_t repository[2048];
} UpdateCheckContext;

typedef struct UpdateCheckMessage {
    int manual;
    int succeeded;
    int update_available;
    UpdateManifest manifest;
    wchar_t error_message[512];
} UpdateCheckMessage;

typedef struct UpdateDownloadContext {
    HWND window;
    UpdateManifest manifest;
} UpdateDownloadContext;

typedef struct UpdateDownloadMessage {
    int succeeded;
    wchar_t staged_path[PATH_BUFFER_COUNT];
    wchar_t error_message[512];
} UpdateDownloadMessage;

typedef struct EditViewSnapshot {
    int first_visible_line;
    DWORD selection_start;
    DWORD selection_end;
    int had_focus;
    int was_at_bottom;
} EditViewSnapshot;

typedef struct AppState {
    HINSTANCE instance;
    HWND window;
    HWND settings_window;
    HFONT font;
    HFONT title_font;
    HFONT section_font;
    HBRUSH background_brush;
    HBRUSH panel_brush;
    HBRUSH input_brush;
    HBRUSH readonly_brush;
    HBRUSH log_brush;
    AppPaths paths;
    AppConfig config;
    LocaleBundle locale;
    LanguageItem languages[MAX_LANGUAGE_ITEMS];
    int language_count;
    wchar_t loaded_language[32];

    HWND edit_token;
    HWND edit_ffmpeg;
    HWND button_download_ffmpeg;
    HWND button_install_ffmpeg;
    HWND edit_ffmpeg_status;
    HWND combo_ugoira;
    HWND edit_sleep;
    HWND edit_parallel;
    HWND edit_profile_page_size;
    HWND combo_language;
    HWND check_metadata;
    HWND check_illustrations;
    HWND check_manga;
    HWND check_novels;
    HWND check_auto_update;
    HWND edit_github_repository;
    HWND button_check_update;
    HWND edit_artist_folder;
    HWND edit_multi_folder;
    HWND edit_single_file;
    HWND edit_multi_file;
    HWND edit_download_dir;
    HWND edit_archive_file;
    HWND button_save;
    HWND button_reload;
    HWND button_auth;
    HWND edit_targets;
    HWND label_targets;
    HWND edit_input_file;
    HWND button_add_target;
    HWND button_delete_target;
    HWND button_delete_all;
    HWND button_view_targets;
    HWND button_view_archive;
    HWND label_counts_summary;
    HWND check_dry_run;
    HWND check_verbose;
    HWND check_print_command;
    HWND button_start;
    HWND button_stop;
    HWND button_show_config;
    HWND button_open_downloads;
    HWND button_readme;
    HWND button_support;
    HWND button_auto_clipboard;
    HWND button_duplicate_download;
    HWND slider_opacity;
    HWND progress_download;
    HWND edit_log;
    HWND button_clear_log;
    int clipboard_listener_registered;
    int auto_clipboard_enabled;
    int allow_duplicate_downloads;
    int planned_download_total;
    int current_list_view;
    int download_stop_requested;
    int update_check_in_progress;
    int update_download_in_progress;
    int window_opacity_percent;
    wchar_t last_clipboard_text[4096];
    wchar_t auth_code_verifier[256];
    int auth_code_waiting;
    HANDLE download_cancel_event;
    wchar_t* target_results_text;
} AppState;

static void save_config_from_ui(AppState* state, int show_language_message);
static void collect_config_from_controls(AppState* state);
static void normalize_target_line(wchar_t* text);
static int save_targets_list_from_ui(AppState* state);
static int save_archive_list_from_ui(AppState* state);
static int save_log_to_file(AppState* state, wchar_t* saved_path, size_t saved_path_count);
static void refresh_current_list_view(AppState* state);
static void set_current_list_view(AppState* state, int view);
static void update_ffmpeg_status(AppState* state);
static void mask_token_for_log(const wchar_t* token, wchar_t* output, size_t output_count);
static int import_authorization_code_from_text(AppState* state, const wchar_t* text, int save_silently, int write_log);
static int count_valid_targets_in_buffer(wchar_t* buffer);
static wchar_t* load_string_array_file_to_wide_lines(const wchar_t* file_path, const char* key);
static int count_string_array_items_from_file(const wchar_t* file_path, const char* key);
static void update_download_counts(AppState* state);
static DWORD WINAPI download_worker_thread(LPVOID parameter);
static LRESULT CALLBACK settings_window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
static void open_settings_window(AppState* state);
static void create_settings_ui(AppState* state);
static void request_stop_download(AppState* state);
static void start_update_check(AppState* state, int manual);
static void apply_window_opacity(AppState* state, int percent);
static int append_unique_line_inplace(wchar_t** buffer, const wchar_t* target, int* added);
static int build_expanded_download_queue(
    const AppConfig* config,
    const wchar_t* raw_targets_text,
    wchar_t* shared_access_token,
    size_t shared_access_token_count,
    wchar_t** expanded_targets_out,
    int* expanded_target_count_out
);

static const wchar_t* state_text(AppState* state, const wchar_t* key)
{
    return locale_text(&state->locale, key);
}

static void play_auto_clipboard_siren(AppState* state)
{
    wchar_t command[PATH_BUFFER_COUNT + 96];

    if (!state || !state->paths.siren_mp3_path[0]) {
        return;
    }
    if (GetFileAttributesW(state->paths.siren_mp3_path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    mciSendStringW(L"close pixiv_auto_clipboard_siren", NULL, 0, NULL);

    StringCchPrintfW(
        command,
        ARRAYSIZE(command),
        L"open \"%ls\" type mpegvideo alias pixiv_auto_clipboard_siren",
        state->paths.siren_mp3_path
    );
    if (mciSendStringW(command, NULL, 0, NULL) != 0) {
        return;
    }

    if (mciSendStringW(L"play pixiv_auto_clipboard_siren from 0", NULL, 0, NULL) != 0) {
        mciSendStringW(L"close pixiv_auto_clipboard_siren", NULL, 0, NULL);
    }
}

static HFONT create_ui_font(int pixel_height, int weight)
{
    return CreateFontW(
        -pixel_height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FF_DONTCARE,
        UI_FONT_NAME
    );
}

static void initialize_theme_resources(AppState* state)
{
    if (!state) {
        return;
    }

    if (!state->font) {
        state->font = create_ui_font(14, FW_NORMAL);
    }
    if (!state->title_font) {
        state->title_font = create_ui_font(21, FW_BOLD);
    }
    if (!state->section_font) {
        state->section_font = create_ui_font(15, FW_BOLD);
    }

    if (!state->background_brush) {
        state->background_brush = CreateSolidBrush(COLOR_APP_BG);
    }
    if (!state->panel_brush) {
        state->panel_brush = CreateSolidBrush(COLOR_PANEL_BG);
    }
    if (!state->input_brush) {
        state->input_brush = CreateSolidBrush(COLOR_INPUT_BG);
    }
    if (!state->readonly_brush) {
        state->readonly_brush = CreateSolidBrush(COLOR_INPUT_READONLY_BG);
    }
    if (!state->log_brush) {
        state->log_brush = CreateSolidBrush(COLOR_LOG_BG);
    }

    if (!state->font) {
        state->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    if (!state->title_font) {
        state->title_font = state->font;
    }
    if (!state->section_font) {
        state->section_font = state->font;
    }
}

static void destroy_theme_resources(AppState* state)
{
    HFONT stock_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT font = NULL;
    HFONT title_font = NULL;
    HFONT section_font = NULL;

    if (!state) {
        return;
    }

    font = state->font;
    title_font = state->title_font;
    section_font = state->section_font;

    if (font && font != stock_font) {
        DeleteObject(font);
    }
    if (title_font && title_font != stock_font && title_font != font) {
        DeleteObject(title_font);
    }
    if (section_font && section_font != stock_font && section_font != font && section_font != title_font) {
        DeleteObject(section_font);
    }

    state->font = NULL;
    state->title_font = NULL;
    state->section_font = NULL;

    if (state->background_brush) {
        DeleteObject(state->background_brush);
        state->background_brush = NULL;
    }
    if (state->panel_brush) {
        DeleteObject(state->panel_brush);
        state->panel_brush = NULL;
    }
    if (state->input_brush) {
        DeleteObject(state->input_brush);
        state->input_brush = NULL;
    }
    if (state->readonly_brush) {
        DeleteObject(state->readonly_brush);
        state->readonly_brush = NULL;
    }
    if (state->log_brush) {
        DeleteObject(state->log_brush);
        state->log_brush = NULL;
    }
}

static void apply_font(HWND hwnd, HFONT font)
{
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static HWND create_control_on(
    AppState* state,
    HWND parent,
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    DWORD ex_style,
    int x,
    int y,
    int width,
    int height,
    int control_id
)
{
    HWND hwnd = CreateWindowExW(
        ex_style,
        class_name,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        (HMENU)(INT_PTR)control_id,
        state->instance,
        NULL
    );
    if (hwnd) {
        apply_font(hwnd, state->font);
    }
    return hwnd;
}

static HWND create_control(
    AppState* state,
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    DWORD ex_style,
    int x,
    int y,
    int width,
    int height,
    int control_id
)
{
    return create_control_on(state, state->window, class_name, text, style, ex_style, x, y, width, height, control_id);
}

static HWND create_label_on(AppState* state, HWND parent, const wchar_t* text, int x, int y, int width)
{
    return create_control_on(state, parent, L"STATIC", text, SS_LEFT | SS_CENTERIMAGE, 0, x, y, width, 28, 0);
}

static HWND create_label(AppState* state, const wchar_t* text, int x, int y, int width)
{
    return create_label_on(state, state->window, text, x, y, width);
}

static HWND create_section_label_on(AppState* state, HWND parent, const wchar_t* text, int x, int y, int width)
{
    HWND hwnd = create_control_on(state, parent, L"STATIC", text, SS_LEFT, 0, x, y, width, 34, 0);
    if (hwnd) {
        apply_font(hwnd, state->section_font);
    }
    return hwnd;
}

static HWND create_section_label(AppState* state, const wchar_t* text, int x, int y, int width)
{
    return create_section_label_on(state, state->window, text, x, y, width);
}

static HWND create_edit_ex_on(AppState* state, HWND parent, const wchar_t* text, int x, int y, int width, int height, int control_id, DWORD extra_style, DWORD ex_style)
{
    DWORD style = WS_TABSTOP | extra_style;
    if ((extra_style & ES_MULTILINE) == 0) {
        style |= ES_AUTOHSCROLL;
    }
    if ((ex_style & WS_EX_CLIENTEDGE) == 0) {
        style |= WS_BORDER;
    }
    HWND hwnd = create_control_on(state, parent, L"EDIT", text, style, ex_style, x, y, width, height, control_id);
    if (hwnd) {
        SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    }
    return hwnd;
}

static HWND create_edit_ex(AppState* state, const wchar_t* text, int x, int y, int width, int height, int control_id, DWORD extra_style, DWORD ex_style)
{
    return create_edit_ex_on(state, state->window, text, x, y, width, height, control_id, extra_style, ex_style);
}

static HWND create_edit_on(AppState* state, HWND parent, const wchar_t* text, int x, int y, int width, int height, int control_id, DWORD extra_style)
{
    return create_edit_ex_on(state, parent, text, x, y, width, height, control_id, extra_style, 0);
}

static HWND create_edit(AppState* state, const wchar_t* text, int x, int y, int width, int height, int control_id, DWORD extra_style)
{
    return create_edit_ex(state, text, x, y, width, height, control_id, extra_style, 0);
}

static HWND create_button_on(AppState* state, HWND parent, const wchar_t* text, int x, int y, int width, int height, int control_id)
{
    return create_control_on(state, parent, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, 0, x, y, width, height, control_id);
}

static HWND create_button(AppState* state, const wchar_t* text, int x, int y, int width, int height, int control_id)
{
    return create_button_on(state, state->window, text, x, y, width, height, control_id);
}

static HWND create_checkbox_on(AppState* state, HWND parent, const wchar_t* text, int x, int y, int width, int height, int control_id)
{
    return create_control_on(state, parent, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, 0, x, y, width, height < 28 ? 28 : height, control_id);
}

static HWND create_checkbox(AppState* state, const wchar_t* text, int x, int y, int width, int height, int control_id)
{
    return create_checkbox_on(state, state->window, text, x, y, width, height, control_id);
}

static HWND create_groupbox(AppState* state, const wchar_t* text, int x, int y, int width, int height)
{
    return create_control(state, L"BUTTON", text, BS_GROUPBOX, 0, x, y, width, height, 0);
}

static void draw_rounded_card(HDC dc, const RECT* bounds)
{
    RECT shadow = *bounds;
    RECT accent;
    HGDIOBJ old_brush = NULL;
    HGDIOBJ old_pen = NULL;
    int accent_left = 0;

    OffsetRect(&shadow, 0, 2);

    old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    old_pen = SelectObject(dc, GetStockObject(DC_PEN));

    SetDCBrushColor(dc, COLOR_PANEL_SHADOW);
    SetDCPenColor(dc, COLOR_PANEL_SHADOW);
    RoundRect(dc, shadow.left, shadow.top, shadow.right, shadow.bottom, PANEL_RADIUS, PANEL_RADIUS);

    SetDCBrushColor(dc, COLOR_PANEL_BG);
    SetDCPenColor(dc, COLOR_PANEL_BORDER);
    RoundRect(dc, bounds->left, bounds->top, bounds->right, bounds->bottom, PANEL_RADIUS, PANEL_RADIUS);

    accent = *bounds;
    accent_left = bounds->left + 222;
    if (accent_left > bounds->right - 80) {
        accent_left = bounds->left + 22;
    }
    accent.left = accent_left;
    accent.right -= 22;
    accent.top += 14;
    accent.bottom = accent.top + 5;
    SetDCBrushColor(dc, COLOR_PANEL_ACCENT);
    SetDCPenColor(dc, COLOR_PANEL_ACCENT);
    RoundRect(dc, accent.left, accent.top, accent.right, accent.bottom, 7, 7);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
}

static void paint_window_background(AppState* state, HDC dc)
{
    RECT client;
    RECT download_panel = { 12, 104, 1268, 658 };
    RECT log_panel = { 12, 672, 1268, 948 };
    RECT title_rect = { 24, 24, 920, 56 };
    RECT subtitle_rect = { 26, 54, 1188, 78 };
    HFONT old_font = NULL;

    if (!state || !dc) {
        return;
    }

    GetClientRect(state->window, &client);
    FillRect(dc, &client, state->background_brush);

    draw_rounded_card(dc, &download_panel);
    draw_rounded_card(dc, &log_panel);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COLOR_TEXT);
    old_font = (HFONT)SelectObject(dc, state->title_font ? state->title_font : state->font);
    DrawTextW(dc, APP_TITLE, -1, &title_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, state->font ? state->font : old_font);
    DrawTextW(dc, state_text(state, L"subtitle"), -1, &subtitle_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (old_font) {
        SelectObject(dc, old_font);
    }
}

static void paint_settings_window_background(AppState* state, HDC dc, HWND hwnd)
{
    RECT client;
    RECT settings_panel = { 12, 12, 658, 296 };
    RECT naming_panel = { 12, 308, 658, 500 };
    RECT paths_panel = { 12, 512, 658, 748 };

    if (!state || !dc || !hwnd) {
        return;
    }

    GetClientRect(hwnd, &client);
    FillRect(dc, &client, state->background_brush);
    draw_rounded_card(dc, &settings_panel);
    draw_rounded_card(dc, &naming_panel);
    draw_rounded_card(dc, &paths_panel);
}

static int settings_controls_available(const AppState* state)
{
    return state
        && state->settings_window
        && state->edit_token
        && state->edit_ffmpeg
        && state->edit_ffmpeg_status
        && state->combo_ugoira
        && state->edit_sleep
        && state->edit_parallel
        && state->edit_profile_page_size
        && state->combo_language
        && state->check_metadata
        && state->check_auto_update
        && state->edit_github_repository
        && state->button_check_update
        && state->edit_artist_folder
        && state->edit_multi_folder
        && state->edit_single_file
        && state->edit_multi_file
        && state->edit_download_dir
        && state->edit_archive_file
        && state->button_save
        && state->button_reload
        && state->button_auth;
}

static void clear_settings_control_handles(AppState* state)
{
    if (!state) {
        return;
    }

    state->edit_token = NULL;
    state->edit_ffmpeg = NULL;
    state->button_download_ffmpeg = NULL;
    state->button_install_ffmpeg = NULL;
    state->edit_ffmpeg_status = NULL;
    state->combo_ugoira = NULL;
    state->edit_sleep = NULL;
    state->edit_parallel = NULL;
    state->edit_profile_page_size = NULL;
    state->combo_language = NULL;
    state->check_metadata = NULL;
    state->check_auto_update = NULL;
    state->edit_github_repository = NULL;
    state->button_check_update = NULL;
    state->edit_artist_folder = NULL;
    state->edit_multi_folder = NULL;
    state->edit_single_file = NULL;
    state->edit_multi_file = NULL;
    state->edit_download_dir = NULL;
    state->edit_archive_file = NULL;
    state->button_save = NULL;
    state->button_reload = NULL;
    state->button_auth = NULL;
}

static void clear_target_results_text(AppState* state)
{
    if (!state) {
        return;
    }
    free(state->target_results_text);
    state->target_results_text = NULL;
}

static int set_target_results_text(AppState* state, const wchar_t* text)
{
    size_t length = 0;
    wchar_t* copy = NULL;

    if (!state) {
        return 0;
    }

    clear_target_results_text(state);
    if (!text) {
        return 1;
    }

    length = wcslen(text);
    copy = (wchar_t*)calloc(length + 1, sizeof(wchar_t));
    if (!copy) {
        return 0;
    }

    if (length > 0) {
        wcscpy_s(copy, length + 1, text);
    }
    state->target_results_text = copy;
    return 1;
}

static int edit_control_is_at_bottom(HWND edit)
{
    SCROLLINFO scroll_info;

    if (!edit) {
        return 1;
    }

    ZeroMemory(&scroll_info, sizeof(scroll_info));
    scroll_info.cbSize = sizeof(scroll_info);
    scroll_info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (GetScrollInfo(edit, SB_VERT, &scroll_info)) {
        return scroll_info.nPos + (int)scroll_info.nPage >= scroll_info.nMax;
    }
    return 1;
}

static void capture_edit_view(HWND edit, EditViewSnapshot* snapshot)
{
    if (!snapshot) {
        return;
    }

    ZeroMemory(snapshot, sizeof(*snapshot));
    if (!edit) {
        return;
    }

    snapshot->first_visible_line = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(
        edit,
        EM_GETSEL,
        (WPARAM)&snapshot->selection_start,
        (LPARAM)&snapshot->selection_end
    );
    snapshot->had_focus = GetFocus() == edit;
    snapshot->was_at_bottom = edit_control_is_at_bottom(edit);
}

static void restore_edit_view(HWND edit, const EditViewSnapshot* snapshot, int follow_bottom)
{
    int text_length = 0;
    int current_first_visible_line = 0;
    DWORD selection_start = 0;
    DWORD selection_end = 0;

    if (!edit || !snapshot) {
        return;
    }

    text_length = GetWindowTextLengthW(edit);
    selection_start = snapshot->selection_start > (DWORD)text_length
        ? (DWORD)text_length
        : snapshot->selection_start;
    selection_end = snapshot->selection_end > (DWORD)text_length
        ? (DWORD)text_length
        : snapshot->selection_end;

    if (follow_bottom && snapshot->was_at_bottom && !snapshot->had_focus) {
        SendMessageW(edit, EM_SETSEL, (WPARAM)text_length, (LPARAM)text_length);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        return;
    }

    SendMessageW(edit, EM_SETSEL, (WPARAM)selection_start, (LPARAM)selection_end);
    current_first_visible_line = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (current_first_visible_line != snapshot->first_visible_line) {
        SendMessageW(
            edit,
            EM_LINESCROLL,
            0,
            (LPARAM)(snapshot->first_visible_line - current_first_visible_line)
        );
    }
}

static void set_edit_text_preserve_view(HWND edit, const wchar_t* text)
{
    EditViewSnapshot snapshot;

    if (!edit) {
        return;
    }

    capture_edit_view(edit, &snapshot);
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(edit, text ? text : L"");
    restore_edit_view(edit, &snapshot, 0);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, NULL, TRUE);
}

static void set_settings_controls_enabled(AppState* state, int enabled)
{
    if (!settings_controls_available(state)) {
        return;
    }

    EnableWindow(state->edit_token, enabled);
    EnableWindow(state->edit_ffmpeg, enabled);
    EnableWindow(state->button_download_ffmpeg, enabled);
    EnableWindow(state->button_install_ffmpeg, enabled);
    EnableWindow(state->combo_ugoira, enabled);
    EnableWindow(state->edit_sleep, enabled);
    EnableWindow(state->edit_parallel, enabled);
    EnableWindow(state->edit_profile_page_size, enabled);
    EnableWindow(state->combo_language, enabled);
    EnableWindow(state->check_metadata, enabled);
    EnableWindow(state->check_auto_update, enabled);
    EnableWindow(state->edit_github_repository, enabled);
    EnableWindow(state->button_check_update, enabled);
    EnableWindow(state->edit_artist_folder, enabled);
    EnableWindow(state->edit_multi_folder, enabled);
    EnableWindow(state->edit_single_file, enabled);
    EnableWindow(state->edit_multi_file, enabled);
    EnableWindow(state->edit_download_dir, enabled);
    EnableWindow(state->edit_archive_file, enabled);
    EnableWindow(state->button_save, enabled);
    EnableWindow(state->button_reload, enabled);
    EnableWindow(state->button_auth, enabled);
}

static void update_progress_theme(AppState* state)
{
    if (!state || !state->progress_download) {
        return;
    }

    SendMessageW(state->progress_download, PBM_SETBARCOLOR, 0, COLOR_ACCENT);
    SendMessageW(state->progress_download, PBM_SETBKCOLOR, 0, COLOR_PANEL_ACCENT);
}

static void apply_window_opacity(AppState* state, int percent)
{
    BYTE alpha = 255;

    if (!state || !state->window) {
        return;
    }

    if (percent < 35) {
        percent = 35;
    } else if (percent > 100) {
        percent = 100;
    }

    state->window_opacity_percent = percent;
    alpha = (BYTE)((255 * percent) / 100);
    SetLayeredWindowAttributes(state->window, 0, alpha, LWA_ALPHA);

    if (state->slider_opacity) {
        SendMessageW(state->slider_opacity, TBM_SETPOS, TRUE, percent);
    }
}

static int is_readonly_display_control(AppState* state, HWND control)
{
    return control == state->edit_ffmpeg_status || control == state->edit_targets || control == state->edit_log;
}

static HBRUSH pick_background_brush_for_control(AppState* state, HWND control, COLORREF* color)
{
    RECT rect;

    if (!state || !control) {
        if (color) {
            *color = COLOR_APP_BG;
        }
        return NULL;
    }

    if (control == state->edit_log) {
        if (color) {
            *color = COLOR_LOG_BG;
        }
        return state->log_brush;
    }

    if (control == state->edit_ffmpeg_status || control == state->edit_targets) {
        if (color) {
            *color = COLOR_INPUT_READONLY_BG;
        }
        return state->readonly_brush;
    }

    if (state->settings_window && GetParent(control) == state->settings_window) {
        if (color) {
            *color = COLOR_PANEL_BG;
        }
        return state->panel_brush;
    }

    GetWindowRect(control, &rect);
    MapWindowPoints(HWND_DESKTOP, state->window, (LPPOINT)&rect, 2);

    if (rect.top < 60) {
        if (color) {
            *color = COLOR_APP_BG;
        }
        return state->background_brush;
    }

    if (color) {
        *color = COLOR_PANEL_BG;
    }
    return state->panel_brush;
}

static void resolve_button_palette(AppState* state, UINT control_id, UINT item_state, COLORREF* fill, COLORREF* border, COLORREF* text)
{
    int pressed = (item_state & ODS_SELECTED) != 0;
    int disabled = (item_state & ODS_DISABLED) != 0;

    *fill = COLOR_BUTTON_NEUTRAL;
    *border = COLOR_PANEL_BORDER;
    *text = COLOR_TEXT;

    if (control_id == IDC_BUTTON_START || control_id == IDC_BUTTON_ADD_TARGET || control_id == IDC_BUTTON_SAVE) {
        *fill = pressed ? COLOR_ACCENT_DARK : COLOR_ACCENT;
        *border = COLOR_ACCENT_DARK;
        *text = RGB(255, 255, 255);
    } else if (control_id == IDC_BUTTON_DELETE_TARGET || control_id == IDC_BUTTON_DELETE_ALL) {
        *fill = pressed ? COLOR_DANGER_DARK : COLOR_DANGER;
        *border = COLOR_DANGER_DARK;
        *text = RGB(255, 255, 255);
    } else if (control_id == IDC_BUTTON_VIEW_TARGETS) {
        if (state && state->current_list_view == LIST_VIEW_TARGETS) {
            *fill = pressed ? COLOR_ACCENT_DARK : COLOR_ACCENT;
            *border = COLOR_ACCENT_DARK;
            *text = RGB(255, 255, 255);
        } else {
            *fill = pressed ? COLOR_BUTTON_NEUTRAL_DARK : COLOR_BUTTON_NEUTRAL;
            *border = COLOR_PANEL_BORDER;
            *text = COLOR_TEXT;
        }
    } else if (control_id == IDC_BUTTON_VIEW_ARCHIVE) {
        if (state && state->current_list_view == LIST_VIEW_ARCHIVE) {
            *fill = pressed ? COLOR_ACCENT_DARK : COLOR_ACCENT;
            *border = COLOR_ACCENT_DARK;
            *text = RGB(255, 255, 255);
        } else {
            *fill = pressed ? COLOR_BUTTON_NEUTRAL_DARK : COLOR_BUTTON_NEUTRAL;
            *border = COLOR_PANEL_BORDER;
            *text = COLOR_TEXT;
        }
    } else if (control_id == IDC_BUTTON_DUPLICATE_DOWNLOAD) {
        if (state && state->allow_duplicate_downloads) {
            *fill = pressed ? COLOR_ACCENT_DARK : COLOR_ACCENT;
            *border = COLOR_ACCENT_DARK;
            *text = RGB(255, 255, 255);
        } else {
            *fill = pressed ? COLOR_BUTTON_NEUTRAL_DARK : COLOR_BUTTON_NEUTRAL;
            *border = COLOR_PANEL_BORDER;
            *text = COLOR_TEXT;
        }
    } else if (control_id == IDC_BUTTON_AUTO_CLIPBOARD) {
        if (state && state->auto_clipboard_enabled) {
            *fill = pressed ? COLOR_ACCENT_DARK : COLOR_ACCENT;
            *border = COLOR_ACCENT_DARK;
            *text = RGB(255, 255, 255);
        } else {
            *fill = pressed ? COLOR_BUTTON_NEUTRAL_DARK : COLOR_BUTTON_NEUTRAL;
            *border = COLOR_PANEL_BORDER;
            *text = COLOR_TEXT;
        }
    } else if (control_id == IDC_BUTTON_STOP) {
        *fill = pressed ? RGB(229, 234, 242) : RGB(246, 248, 252);
        *border = RGB(193, 204, 219);
        *text = COLOR_TEXT;
    } else if (control_id == IDC_BUTTON_AUTH) {
        *fill = pressed ? RGB(207, 228, 249) : COLOR_PANEL_ACCENT;
        *border = COLOR_ACCENT;
        *text = COLOR_ACCENT_DARK;
    } else {
        *fill = pressed ? COLOR_BUTTON_NEUTRAL_DARK : COLOR_BUTTON_NEUTRAL;
        *border = COLOR_PANEL_BORDER;
        *text = COLOR_TEXT;
    }

    if (disabled) {
        *fill = RGB(239, 243, 248);
        *border = RGB(218, 226, 236);
        *text = COLOR_MUTED_TEXT;
    }
}

static LRESULT draw_owner_button(AppState* state, const DRAWITEMSTRUCT* draw)
{
    wchar_t text_buffer[256];
    RECT rect;
    RECT text_rect;
    COLORREF fill;
    COLORREF border;
    COLORREF text;
    HGDIOBJ old_brush = NULL;
    HGDIOBJ old_pen = NULL;

    if (!draw || draw->CtlType != ODT_BUTTON) {
        return FALSE;
    }

    resolve_button_palette(state, draw->CtlID, draw->itemState, &fill, &border, &text);
    rect = draw->rcItem;

    old_brush = SelectObject(draw->hDC, GetStockObject(DC_BRUSH));
    old_pen = SelectObject(draw->hDC, GetStockObject(DC_PEN));

    SetDCBrushColor(draw->hDC, fill);
    SetDCPenColor(draw->hDC, border);
    RoundRect(draw->hDC, rect.left, rect.top, rect.right, rect.bottom, 10, 10);

    if (draw->itemState & ODS_FOCUS) {
        RECT focus_rect = rect;
        InflateRect(&focus_rect, -4, -4);
        SetDCBrushColor(draw->hDC, fill);
        SetDCPenColor(draw->hDC, RGB(184, 137, 72));
        RoundRect(draw->hDC, focus_rect.left, focus_rect.top, focus_rect.right, focus_rect.bottom, 8, 8);
    }

    GetWindowTextW(draw->hwndItem, text_buffer, 256);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, text);
    text_rect = rect;
    InflateRect(&text_rect, -5, -1);
    DrawTextW(draw->hDC, text_buffer, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(draw->hDC, old_brush);
    SelectObject(draw->hDC, old_pen);
    return TRUE;
}

static void append_log(AppState* state, const wchar_t* text)
{
    int length = 0;
    EditViewSnapshot snapshot;

    if (!state || !state->edit_log || !text) {
        return;
    }

    capture_edit_view(state->edit_log, &snapshot);
    SendMessageW(state->edit_log, WM_SETREDRAW, FALSE, 0);
    length = GetWindowTextLengthW(state->edit_log);
    SendMessageW(state->edit_log, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    SendMessageW(state->edit_log, EM_REPLACESEL, FALSE, (LPARAM)text);
    restore_edit_view(state->edit_log, &snapshot, 1);
    SendMessageW(state->edit_log, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state->edit_log, NULL, TRUE);
}

static void append_config_line(AppState* state, const wchar_t* label, const wchar_t* value)
{
    append_log(state, label);
    append_log(state, L": ");
    append_log(state, value && value[0] ? value : L"(empty)");
    append_log(state, L"\r\n");
}

static void append_log_line(AppState* state, const wchar_t* text)
{
    append_log(state, text);
    append_log(state, L"\r\n");
}

static void append_verbose_diagnostic(AppState* state)
{
    wchar_t detail[4096];

    if (!state || Button_GetCheck(state->check_verbose) != BST_CHECKED) {
        return;
    }
    if (!pixiv_get_last_diagnostic(detail, _countof(detail)) || !detail[0]) {
        return;
    }

    append_log(state, L"[detail] ");
    append_log_line(state, detail);
}

static int save_log_to_file(AppState* state, wchar_t* saved_path, size_t saved_path_count)
{
    int text_length = 0;
    wchar_t* log_text = NULL;
    char* log_text_utf8 = NULL;
    wchar_t log_path[PATH_BUFFER_COUNT];
    wchar_t log_directory[PATH_BUFFER_COUNT];
    int written = 0;

    if (saved_path && saved_path_count > 0) {
        saved_path[0] = L'\0';
    }
    if (!state || !state->edit_log) {
        return 0;
    }

    resolve_against_app_root(&state->paths, L"config/log.txt", log_path, PATH_BUFFER_COUNT);
    safe_wcs_copy(log_directory, PATH_BUFFER_COUNT, log_path);
    PathRemoveFileSpecW(log_directory);
    SHCreateDirectoryExW(NULL, log_directory, NULL);

    text_length = GetWindowTextLengthW(state->edit_log);
    log_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
    if (!log_text) {
        return 0;
    }

    if (text_length > 0) {
        GetWindowTextW(state->edit_log, log_text, text_length + 1);
    }

    log_text_utf8 = wide_to_utf8_alloc(log_text);
    if (log_text_utf8) {
        written = write_text_file_utf8(log_path, log_text_utf8);
    }

    if (written && saved_path && saved_path_count > 0) {
        safe_wcs_copy(saved_path, saved_path_count, log_path);
    }

    free(log_text_utf8);
    free(log_text);
    return written;
}

static void free_download_worker_context(DownloadWorkerContext* context)
{
    if (!context) {
        return;
    }
    free(context->targets_text);
    free(context);
}

static DWORD WINAPI parallel_download_worker_thread(LPVOID parameter)
{
    ParallelDownloadContext* parallel = (ParallelDownloadContext*)parameter;
    DownloadWorkerContext* context = NULL;
    wchar_t access_token[2048];

    if (!parallel || !parallel->owner) {
        return 0;
    }

    context = parallel->owner;
    safe_wcs_copy(access_token, _countof(access_token), context->shared_access_token);

    while (1) {
        LONG target_index = 0;
        LONG completed_count = 0;
        wchar_t result_message[2048];
        wchar_t detail_message[4096];
        const wchar_t* target = NULL;
        int succeeded = 0;
        int attempt = 0;
        DownloadResultMessage* message = NULL;

        if (context->cancel_event && WaitForSingleObject(context->cancel_event, 0) == WAIT_OBJECT_0) {
            break;
        }

        target_index = InterlockedIncrement(&parallel->next_target_index) - 1;
        if (target_index < 0 || target_index >= parallel->target_count) {
            break;
        }
        if (context->cancel_event && WaitForSingleObject(context->cancel_event, 0) == WAIT_OBJECT_0) {
            break;
        }

        target = parallel->targets[target_index];
        result_message[0] = L'\0';
        detail_message[0] = L'\0';
        for (attempt = 0; attempt < DOWNLOAD_TARGET_ATTEMPTS; ++attempt) {
            pixiv_clear_last_diagnostic();
            succeeded = pixiv_download_artwork_with_access_token(
                &context->paths,
                &context->config,
                target,
                access_token,
                _countof(access_token),
                result_message,
                _countof(result_message)
            );
            if (succeeded) {
                detail_message[0] = L'\0';
                break;
            }

            pixiv_get_last_diagnostic(detail_message, _countof(detail_message));
            if (attempt + 1 >= DOWNLOAD_TARGET_ATTEMPTS
                || (context->cancel_event && WaitForSingleObject(context->cancel_event, 0) == WAIT_OBJECT_0)) {
                break;
            }
            pixiv_sleep_between_requests(&context->config);
        }

        if (succeeded) {
            InterlockedIncrement(&parallel->success_count);
        } else {
            InterlockedIncrement(&parallel->failure_count);
        }
        completed_count = InterlockedIncrement(&parallel->completed_count);

        message = (DownloadResultMessage*)calloc(1, sizeof(DownloadResultMessage));
        if (message) {
            message->total_count = (int)completed_count;
            message->succeeded = succeeded;
            safe_wcs_copy(message->target, _countof(message->target), target);
            safe_wcs_copy(message->result_message, _countof(message->result_message), result_message);
            if (!succeeded) {
                safe_wcs_copy(message->detail_message, _countof(message->detail_message), detail_message);
            }
            if (!PostMessageW(context->window, WM_APP_DOWNLOAD_RESULT, 0, (LPARAM)message)) {
                free(message);
            }
        }

        if (context->cancel_event && WaitForSingleObject(context->cancel_event, 0) == WAIT_OBJECT_0) {
            break;
        }
        pixiv_sleep_between_requests(&context->config);
    }

    return 0;
}

static DWORD WINAPI download_worker_thread(LPVOID parameter)
{
    DownloadWorkerContext* context = (DownloadWorkerContext*)parameter;
    DownloadPreparedMessage* prepared = NULL;
    wchar_t* token_context = NULL;
    wchar_t* line = NULL;
    wchar_t* expanded_targets = NULL;
    wchar_t** targets = NULL;
    wchar_t access_token_error[512];
    int total_count = 0;
    int success_count = 0;
    int failure_count = 0;
    int planned_total = 0;
    int cancelled = 0;
    int target_count = 0;

    if (!context) {
        return 0;
    }

    pixiv_set_detailed_logging(context->verbose_enabled);

    if (context->targets_text) {
        access_token_error[0] = L'\0';
        prepared = (DownloadPreparedMessage*)calloc(1, sizeof(DownloadPreparedMessage));
        if (!prepared) {
            goto finish;
        }

        if (!pixiv_prepare_access_token(&context->config, context->shared_access_token, _countof(context->shared_access_token), access_token_error, _countof(access_token_error))) {
            prepared->failed = 1;
            safe_wcs_copy(prepared->error_message, _countof(prepared->error_message), access_token_error);
            pixiv_get_last_diagnostic(prepared->detail_message, _countof(prepared->detail_message));
            PostMessageW(context->window, WM_APP_DOWNLOAD_PREPARED, 0, (LPARAM)prepared);
            prepared = NULL;
            goto finish;
        }

        if (!build_expanded_download_queue(
                &context->config,
                context->targets_text,
                context->shared_access_token,
                _countof(context->shared_access_token),
                &expanded_targets,
                &planned_total)
            || planned_total <= 0) {
            prepared->failed = 1;
            safe_wcs_copy(prepared->error_message, _countof(prepared->error_message), L"No valid Pixiv targets were found.");
            pixiv_get_last_diagnostic(prepared->detail_message, _countof(prepared->detail_message));
            PostMessageW(context->window, WM_APP_DOWNLOAD_PREPARED, 0, (LPARAM)prepared);
            prepared = NULL;
            goto finish;
        }

        free(context->targets_text);
        context->targets_text = expanded_targets;
        expanded_targets = NULL;

        prepared->expanded_targets_text = _wcsdup(context->targets_text);
        targets = (wchar_t**)calloc((size_t)planned_total, sizeof(wchar_t*));
        if (!targets) {
            prepared->failed = 1;
            safe_wcs_copy(prepared->error_message, _countof(prepared->error_message), L"Could not allocate the parallel download queue.");
            PostMessageW(context->window, WM_APP_DOWNLOAD_PREPARED, 0, (LPARAM)prepared);
            prepared = NULL;
            goto finish;
        }

        line = wcstok_s(context->targets_text, L"\r\n", &token_context);
        while (line) {
            normalize_target_line(line);
            if (line[0] && line[0] != L'#' && target_count < planned_total) {
                targets[target_count++] = line;
            }
            line = wcstok_s(NULL, L"\r\n", &token_context);
        }

        if (target_count <= 0) {
            prepared->failed = 1;
            safe_wcs_copy(prepared->error_message, _countof(prepared->error_message), L"No valid Pixiv targets were found.");
            PostMessageW(context->window, WM_APP_DOWNLOAD_PREPARED, 0, (LPARAM)prepared);
            prepared = NULL;
            goto finish;
        }

        prepared->prepared_count = target_count;
        SendMessageW(context->window, WM_APP_DOWNLOAD_PREPARED, 0, (LPARAM)prepared);
        prepared = NULL;

        {
            ParallelDownloadContext parallel;
            HANDLE worker_threads[MAX_PARALLEL_DOWNLOADS];
            int requested_workers = context->config.parallel_downloads;
            int created_workers = 0;
            int index = 0;

            ZeroMemory(&parallel, sizeof(parallel));
            ZeroMemory(worker_threads, sizeof(worker_threads));
            parallel.owner = context;
            parallel.targets = targets;
            parallel.target_count = target_count;

            if (requested_workers < MIN_PARALLEL_DOWNLOADS) {
                requested_workers = MIN_PARALLEL_DOWNLOADS;
            } else if (requested_workers > MAX_PARALLEL_DOWNLOADS) {
                requested_workers = MAX_PARALLEL_DOWNLOADS;
            }
            if (requested_workers > target_count) {
                requested_workers = target_count;
            }

            for (index = 0; index < requested_workers; ++index) {
                HANDLE thread = CreateThread(NULL, 0, parallel_download_worker_thread, &parallel, 0, NULL);
                if (thread) {
                    worker_threads[created_workers++] = thread;
                }
            }

            if (created_workers > 0) {
                WaitForMultipleObjects((DWORD)created_workers, worker_threads, TRUE, INFINITE);
                for (index = 0; index < created_workers; ++index) {
                    CloseHandle(worker_threads[index]);
                }
            } else {
                parallel_download_worker_thread(&parallel);
            }

            total_count = (int)parallel.completed_count;
            success_count = (int)parallel.success_count;
            failure_count = (int)parallel.failure_count;
        }
    }

    if (!cancelled && context->cancel_event && WaitForSingleObject(context->cancel_event, 0) == WAIT_OBJECT_0) {
        cancelled = 1;
    }

finish:
    free(expanded_targets);
    free(targets);
    if (prepared) {
        free(prepared->expanded_targets_text);
        free(prepared);
    }
    {
        DownloadFinishedMessage* finished = (DownloadFinishedMessage*)calloc(1, sizeof(DownloadFinishedMessage));
        if (finished) {
            finished->total_count = total_count;
            finished->success_count = success_count;
            finished->failure_count = failure_count;
            finished->cancelled = cancelled;
            if (!PostMessageW(context->window, WM_APP_DOWNLOAD_FINISHED, 0, (LPARAM)finished)) {
                free(finished);
            }
        }
    }

    free_download_worker_context(context);
    return 0;
}

static void set_download_progress(AppState* state, int current, int total)
{
    if (!state || !state->progress_download) {
        return;
    }

    if (total <= 0) {
        SendMessageW(state->progress_download, PBM_SETRANGE32, 0, 1);
        SendMessageW(state->progress_download, PBM_SETPOS, 0, 0);
        return;
    }

    if (current < 0) {
        current = 0;
    }
    if (current > total) {
        current = total;
    }

    SendMessageW(state->progress_download, PBM_SETRANGE32, 0, total);
    SendMessageW(state->progress_download, PBM_SETPOS, current, 0);
}

static int count_string_array_items_from_file(const wchar_t* file_path, const char* key)
{
    wchar_t* lines = NULL;
    int count = 0;

    if (!file_path || !file_path[0] || !key || !key[0]) {
        return 0;
    }

    lines = load_string_array_file_to_wide_lines(file_path, key);
    if (!lines) {
        return 0;
    }

    count = count_valid_targets_in_buffer(lines);
    free(lines);
    return count;
}

static void update_download_counts(AppState* state)
{
    wchar_t archive_path[PATH_BUFFER_COUNT];
    wchar_t summary[128];
    int target_count = 0;
    int archive_count = 0;

    if (!state || !state->label_counts_summary) {
        return;
    }

    resolve_against_app_root(&state->paths, state->config.archive_file, archive_path, PATH_BUFFER_COUNT);
    target_count = count_string_array_items_from_file(state->paths.targets_path, "targets");
    archive_count = count_string_array_items_from_file(archive_path, "downloaded_links");

    swprintf_s(
        summary,
        _countof(summary),
        L"%ls %d | %ls %d",
        state_text(state, L"label_targets_count"),
        target_count,
        state_text(state, L"label_archive_count"),
        archive_count
    );
    SetWindowTextW(state->label_counts_summary, summary);
}

static int add_pending_target_from_input(AppState* state, int save_list, int clear_input, int write_log)
{
    wchar_t target[4096];

    UNREFERENCED_PARAMETER(save_list);

    if (!state || !state->edit_input_file) {
        return 0;
    }

    GetWindowTextW(state->edit_input_file, target, 4096);
    normalize_target_line(target);
    if (!target[0]) {
        if (clear_input) {
            SetWindowTextW(state->edit_input_file, L"");
        }
        return 0;
    }

    set_current_list_view(state, LIST_VIEW_TARGETS);
    if (append_target_to_targets_storage(state, target, 1)) {
        if (write_log) {
            append_log_line(state, L"\uBAA9\uB85D\uC5D0 \uCD94\uAC00\uD588\uC2B5\uB2C8\uB2E4.");
        }
    } else if (write_log) {
        append_log_line(state, L"\uC774\uBBF8 \uBAA9\uB85D\uC5D0 \uC788\uB294 \uC8FC\uC18C\uC785\uB2C8\uB2E4.");
    }

    if (clear_input) {
        SetWindowTextW(state->edit_input_file, L"");
    }
    return 1;
}

static int delete_selected_target_from_edit(AppState* state)
{
    int text_length = 0;
    wchar_t* current_text = NULL;
    wchar_t* combined = NULL;
    DWORD selection_start = 0;
    DWORD selection_end = 0;
    size_t line_start = 0;
    size_t line_end = 0;
    size_t prefix_length = 0;
    size_t suffix_length = 0;

    if (!state || !state->edit_targets) {
        return 0;
    }

    if (state->current_list_view == LIST_VIEW_NONE) {
        append_log_line(state, L"\uBA3C\uC800 \uC8FC\uC18C \uBCF4\uAE30 \uB610\uB294 \uC544\uCE74\uC774\uBE0C \uAE30\uB85D \uBCF4\uAE30\uB97C \uB20C\uB7EC\uC8FC\uC138\uC694.");
        return 0;
    }

    text_length = GetWindowTextLengthW(state->edit_targets);
    if (text_length <= 0) {
        append_log_line(state, L"\uC0AD\uC81C\uD560 \uD56D\uBAA9\uC774 \uC5C6\uC2B5\uB2C8\uB2E4.");
        return 0;
    }

    current_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
    if (!current_text) {
        append_log_line(state, L"[error] Could not allocate target buffer.");
        return 0;
    }
    GetWindowTextW(state->edit_targets, current_text, text_length + 1);

    SendMessageW(state->edit_targets, EM_GETSEL, (WPARAM)&selection_start, (LPARAM)&selection_end);
    if (selection_start > (DWORD)text_length) {
        selection_start = (DWORD)text_length;
    }
    if (selection_end > (DWORD)text_length) {
        selection_end = (DWORD)text_length;
    }
    if (selection_end < selection_start) {
        DWORD temp = selection_start;
        selection_start = selection_end;
        selection_end = temp;
    }

    line_start = (size_t)selection_start;
    while (line_start > 0 && current_text[line_start - 1] != L'\n' && current_text[line_start - 1] != L'\r') {
        --line_start;
    }

    line_end = (size_t)(selection_end > selection_start ? selection_end : selection_start);
    while (current_text[line_end] && current_text[line_end] != L'\n' && current_text[line_end] != L'\r') {
        ++line_end;
    }
    if (current_text[line_end] == L'\r' && current_text[line_end + 1] == L'\n') {
        line_end += 2;
    } else if (current_text[line_end] == L'\r' || current_text[line_end] == L'\n') {
        ++line_end;
    }

    prefix_length = line_start;
    suffix_length = wcslen(current_text + line_end);
    combined = (wchar_t*)calloc(prefix_length + suffix_length + 1, sizeof(wchar_t));
    if (!combined) {
        free(current_text);
        append_log_line(state, L"[error] Could not allocate target buffer.");
        return 0;
    }

    if (prefix_length > 0) {
        memcpy(combined, current_text, prefix_length * sizeof(wchar_t));
    }
    if (suffix_length > 0) {
        memcpy(combined + prefix_length, current_text + line_end, (suffix_length + 1) * sizeof(wchar_t));
    } else {
        combined[prefix_length] = L'\0';
    }

    SetWindowTextW(state->edit_targets, combined);
    free(combined);
    free(current_text);

    if (state->current_list_view == LIST_VIEW_ARCHIVE) {
        if (!save_archive_list_from_ui(state)) {
            append_log_line(state, L"[error] Could not save config/archive.json.");
            return 0;
        }
    } else {
        if (!save_targets_list_from_ui(state)) {
            append_log_line(state, L"[error] Could not save config/list.json.");
            return 0;
        }
    }

    append_log_line(state, L"\uC120\uD0DD\uD55C \uD56D\uBAA9\uC744 \uC0AD\uC81C\uD588\uC2B5\uB2C8\uB2E4.");
    return 1;
}

static void pump_pending_messages(void)
{
    MSG message;

    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static void trim_wide_text(wchar_t* text)
{
    wchar_t* start = NULL;
    wchar_t* end = NULL;

    if (!text || !text[0]) {
        return;
    }

    start = text;
    while (*start && (iswspace(*start) || *start == 0xFEFF)) {
        ++start;
    }

    if (start != text) {
        MoveMemory(text, start, (wcslen(start) + 1) * sizeof(wchar_t));
    }

    end = text + wcslen(text);
    while (end > text && iswspace(end[-1])) {
        --end;
    }
    *end = L'\0';
}

static void strip_result_marker(wchar_t* text)
{
    size_t length = 0;

    if (!text || !text[0]) {
        return;
    }

    trim_wide_text(text);
    length = wcslen(text);
    if (length == 0) {
        return;
    }

    if (text[length - 1] == 0x2705 || text[length - 1] == 0x274C) {
        text[length - 1] = L'\0';
        trim_wide_text(text);
    }
}

static int parse_positive_decimal_id(const wchar_t* text, int* value_out, const wchar_t** end_out)
{
    unsigned long long value = 0;
    const wchar_t* p = text;

    if (!text || !iswdigit(*text)) {
        return 0;
    }

    while (iswdigit(*p)) {
        value = value * 10 + (unsigned long long)(*p - L'0');
        if (value > 2147483647ULL) {
            return 0;
        }
        ++p;
    }

    if (value == 0) {
        return 0;
    }
    if (value_out) {
        *value_out = (int)value;
    }
    if (end_out) {
        *end_out = p;
    }
    return 1;
}

static int pixiv_url_page_parameter(const wchar_t* text)
{
    const wchar_t* p = text;

    if (!text) {
        return 0;
    }

    while ((p = wcspbrk(p, L"?&")) != NULL) {
        int page_number = 0;

        ++p;
        if ((p[0] == L'p' || p[0] == L'P')
            && p[1] == L'='
            && parse_positive_decimal_id(p + 2, &page_number, NULL)) {
            return page_number;
        }
    }
    return 0;
}

static void canonicalize_pixiv_target_copy(
    const wchar_t* source,
    wchar_t* output,
    size_t output_count
)
{
    const wchar_t* marker = NULL;
    const wchar_t* id_end = NULL;
    int target_id = 0;

    if (!source || !output || output_count == 0) {
        return;
    }
    if (source != output) {
        safe_wcs_copy(output, output_count, source);
    }
    if (!output[0] || !StrStrIW(output, L"pixiv.net/")) {
        return;
    }

    marker = StrStrIW(output, L"/artworks/");
    if (marker && parse_positive_decimal_id(marker + 10, &target_id, NULL)) {
        swprintf_s(output, output_count, L"https://www.pixiv.net/artworks/%d", target_id);
        return;
    }

    marker = StrStrIW(output, L"/novel/show.php");
    if (marker) {
        const wchar_t* id_parameter = StrStrIW(marker, L"id=");
        if (id_parameter && parse_positive_decimal_id(id_parameter + 3, &target_id, NULL)) {
            swprintf_s(output, output_count, L"https://www.pixiv.net/novel/show.php?id=%d", target_id);
            return;
        }
    }

    marker = StrStrIW(output, L"/novels/");
    if (marker && parse_positive_decimal_id(marker + 8, &target_id, NULL)) {
        swprintf_s(output, output_count, L"https://www.pixiv.net/novel/show.php?id=%d", target_id);
        return;
    }

    marker = StrStrIW(output, L"illust_id=");
    if (marker && parse_positive_decimal_id(marker + 10, &target_id, NULL)) {
        swprintf_s(output, output_count, L"https://www.pixiv.net/artworks/%d", target_id);
        return;
    }

    marker = StrStrIW(output, L"/users/");
    if (marker && parse_positive_decimal_id(marker + 7, &target_id, &id_end)) {
        const wchar_t* category = NULL;
        int page_number = pixiv_url_page_parameter(id_end);

        if (_wcsnicmp(id_end, L"/artworks", 9) == 0) {
            category = L"artworks";
        } else if (_wcsnicmp(id_end, L"/illustrations", 14) == 0) {
            category = L"illustrations";
        } else if (_wcsnicmp(id_end, L"/manga", 6) == 0) {
            category = L"manga";
        } else if (_wcsnicmp(id_end, L"/novels", 7) == 0) {
            category = L"novels";
        }

        if (category) {
            if (page_number > 0) {
                swprintf_s(output, output_count, L"https://www.pixiv.net/users/%d/%ls?p=%d", target_id, category, page_number);
            } else {
                swprintf_s(output, output_count, L"https://www.pixiv.net/users/%d/%ls", target_id, category);
            }
        } else if (*id_end == L'\0'
            || *id_end == L'?'
            || *id_end == L'#'
            || (*id_end == L'/' && (id_end[1] == L'\0' || id_end[1] == L'?' || id_end[1] == L'#'))) {
            swprintf_s(output, output_count, L"https://www.pixiv.net/users/%d", target_id);
        }
    }
}

static void normalize_target_line(wchar_t* text)
{
    if (!text) {
        return;
    }

    trim_wide_text(text);
    strip_result_marker(text);
}

static int append_line_to_wide_buffer(wchar_t** buffer, size_t* capacity, size_t* used, const wchar_t* line)
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

static wchar_t* normalized_lines_copy_alloc(const wchar_t* text)
{
    wchar_t* working = NULL;
    wchar_t* combined = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    size_t capacity = 0;
    size_t used = 0;
    size_t length = 0;

    if (!text) {
        return NULL;
    }

    length = wcslen(text);
    working = (wchar_t*)calloc(length + 1, sizeof(wchar_t));
    if (!working) {
        return NULL;
    }
    if (length > 0) {
        wcscpy_s(working, length + 1, text);
    }

    capacity = length + 64;
    combined = (wchar_t*)calloc(capacity, sizeof(wchar_t));
    if (!combined) {
        free(working);
        return NULL;
    }

    line = wcstok_s(working, L"\r\n", &context);
    while (line) {
        normalize_target_line(line);
        if (line[0] && line[0] != L'#') {
            if (!append_line_to_wide_buffer(&combined, &capacity, &used, line)) {
                free(combined);
                free(working);
                return NULL;
            }
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }

    free(working);
    return combined;
}

static void replace_placeholder_in_text(wchar_t* text, size_t text_count, const wchar_t* placeholder, const wchar_t* replacement)
{
    wchar_t buffer[4096];
    wchar_t* position = NULL;

    if (!text || !placeholder || !placeholder[0] || !replacement) {
        return;
    }

    for (;;) {
        position = wcsstr(text, placeholder);
        if (!position) {
            return;
        }

        buffer[0] = L'\0';
        *position = L'\0';
        wcscpy_s(buffer, 4096, text);
        wcscat_s(buffer, 4096, replacement);
        wcscat_s(buffer, 4096, position + wcslen(placeholder));
        safe_wcs_copy(text, text_count, buffer);
    }
}

static void format_state_text_one(AppState* state, const wchar_t* key, const wchar_t* placeholder, const wchar_t* replacement, wchar_t* output, size_t output_count)
{
    safe_wcs_copy(output, output_count, state_text(state, key));
    replace_placeholder_in_text(output, output_count, placeholder, replacement);
}

static void format_state_text_two(
    AppState* state,
    const wchar_t* key,
    const wchar_t* placeholder_one,
    const wchar_t* replacement_one,
    const wchar_t* placeholder_two,
    const wchar_t* replacement_two,
    wchar_t* output,
    size_t output_count
)
{
    safe_wcs_copy(output, output_count, state_text(state, key));
    replace_placeholder_in_text(output, output_count, placeholder_one, replacement_one);
    replace_placeholder_in_text(output, output_count, placeholder_two, replacement_two);
}

static const char* skip_json_ws_local(const char* p)
{
    if (p && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }
    while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
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

static char* extract_json_array_alloc(const char* json, const char* key)
{
    const char* start = NULL;
    const char* end = NULL;
    size_t length = 0;
    int depth = 0;
    char* array_text = NULL;
    char key_pattern[128];

    if (!json || !key || !key[0]) {
        return NULL;
    }

    start = skip_json_ws_local(json);
    if (*start != '[') {
        sprintf_s(key_pattern, sizeof(key_pattern), "\"%s\"", key);
        start = strstr(json, key_pattern);
        if (!start) {
            return NULL;
        }
        start = strchr(start, ':');
        if (!start) {
            return NULL;
        }
        ++start;
        start = skip_json_ws_local(start);
    }

    if (*start != '[') {
        return NULL;
    }

    end = start;
    while (*end) {
        if (*end == '"') {
            end = skip_json_string_local(end);
            continue;
        }
        if (*end == '[') {
            ++depth;
        } else if (*end == ']') {
            --depth;
            if (depth == 0) {
                ++end;
                break;
            }
        }
        ++end;
    }

    if (depth != 0) {
        return NULL;
    }

    length = (size_t)(end - start);
    array_text = (char*)calloc(length + 1, sizeof(char));
    if (!array_text) {
        return NULL;
    }
    memcpy(array_text, start, length);
    array_text[length] = '\0';
    return array_text;
}

static wchar_t* parse_json_string_array_to_wide_lines(const char* json, const char* key)
{
    char* array_text = NULL;
    const char* p = NULL;
    size_t output_size = 0;
    char* output_utf8 = NULL;
    size_t used = 0;
    wchar_t* output_wide = NULL;

    array_text = extract_json_array_alloc(json, key);
    if (!array_text) {
        return NULL;
    }

    output_size = strlen(array_text) + 32;
    output_utf8 = (char*)calloc(output_size, sizeof(char));
    if (!output_utf8) {
        free(array_text);
        return NULL;
    }

    p = skip_json_ws_local(array_text);
    if (*p == '[') {
        ++p;
    }

    while (*p) {
        char item[2048];
        size_t item_length = 0;

        p = skip_json_ws_local(p);
        if (*p == ']') {
            break;
        }
        if (*p != '"') {
            break;
        }
        if (!extract_json_string_local(p, item, sizeof(item))) {
            break;
        }

        item_length = strlen(item);
        if (used + item_length + 3 >= output_size) {
            char* grown = (char*)realloc(output_utf8, output_size * 2 + item_length + 32);
            if (!grown) {
                free(output_utf8);
                free(array_text);
                return NULL;
            }
            output_utf8 = grown;
            output_size = output_size * 2 + item_length + 32;
        }

        memcpy(output_utf8 + used, item, item_length);
        used += item_length;
        output_utf8[used++] = '\r';
        output_utf8[used++] = '\n';
        output_utf8[used] = '\0';

        p = skip_json_string_local(p);
        p = skip_json_ws_local(p);
        if (*p == ',') {
            ++p;
        }
    }

    if (used >= 2 && output_utf8[used - 2] == '\r' && output_utf8[used - 1] == '\n') {
        output_utf8[used - 2] = '\0';
    }

    output_wide = utf8_to_wide_alloc(output_utf8);
    free(output_utf8);
    free(array_text);
    return output_wide;
}

static wchar_t* load_string_array_file_to_wide_lines(const wchar_t* file_path, const char* key)
{
    char* json = NULL;
    wchar_t* lines = NULL;

    if (!file_path || !file_path[0] || !key || !key[0]) {
        return NULL;
    }

    json = read_text_file_utf8(file_path);
    if (!json) {
        return NULL;
    }

    lines = parse_json_string_array_to_wide_lines(json, key);
    free(json);
    return lines;
}

static int save_targets_list_from_ui(AppState* state)
{
    int text_length = 0;
    wchar_t* targets_text = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    size_t buffer_size = 0;
    char* json_buffer = NULL;
    wchar_t list_directory[PATH_BUFFER_COUNT];
    int first_item = 1;

    if (!state) {
        return 0;
    }

    text_length = GetWindowTextLengthW(state->edit_targets);
    targets_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
    if (!targets_text) {
        return 0;
    }
    if (text_length > 0) {
        GetWindowTextW(state->edit_targets, targets_text, text_length + 1);
    }

    buffer_size = ((size_t)text_length + 1) * 12 + 128;
    json_buffer = (char*)calloc(buffer_size, sizeof(char));
    if (!json_buffer) {
        free(targets_text);
        return 0;
    }

    strcpy_s(json_buffer, buffer_size, "{\n  \"targets\": [\n");
    line = wcstok_s(targets_text, L"\r\n", &context);
    while (line) {
        char* item_utf8 = NULL;
        char escaped[4096];

        normalize_target_line(line);
        if (line[0] && line[0] != L'#') {
            item_utf8 = wide_to_utf8_alloc(line);
            if (item_utf8) {
                json_escape_string(item_utf8, escaped, sizeof(escaped));
                if (!first_item) {
                    strcat_s(json_buffer, buffer_size, ",\n");
                }
                strcat_s(json_buffer, buffer_size, "    \"");
                strcat_s(json_buffer, buffer_size, escaped);
                strcat_s(json_buffer, buffer_size, "\"");
                first_item = 0;
            }
            free(item_utf8);
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }
    strcat_s(json_buffer, buffer_size, "\n  ]\n}\n");

    safe_wcs_copy(list_directory, PATH_BUFFER_COUNT, state->paths.targets_path);
    PathRemoveFileSpecW(list_directory);
    SHCreateDirectoryExW(NULL, list_directory, NULL);

    if (!write_text_file_utf8(state->paths.targets_path, json_buffer)) {
        free(json_buffer);
        free(targets_text);
        return 0;
    }

    free(json_buffer);
    free(targets_text);
    if (state->planned_download_total <= 0
        && state->current_list_view == LIST_VIEW_TARGETS
        && state->edit_targets) {
        int updated_length = GetWindowTextLengthW(state->edit_targets);
        wchar_t* updated_text = (wchar_t*)calloc((size_t)updated_length + 1, sizeof(wchar_t));
        if (updated_text) {
            if (updated_length > 0) {
                GetWindowTextW(state->edit_targets, updated_text, updated_length + 1);
            }
            set_target_results_text(state, updated_text);
            free(updated_text);
        }
    }
    update_download_counts(state);
    return 1;
}

static int save_archive_list_from_ui(AppState* state)
{
    int text_length = 0;
    wchar_t* archive_text = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    size_t buffer_size = 0;
    char* json_buffer = NULL;
    wchar_t archive_path[PATH_BUFFER_COUNT];
    wchar_t archive_directory[PATH_BUFFER_COUNT];
    int first_item = 1;

    if (!state) {
        return 0;
    }

    resolve_against_app_root(&state->paths, state->config.archive_file, archive_path, PATH_BUFFER_COUNT);

    text_length = GetWindowTextLengthW(state->edit_targets);
    archive_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
    if (!archive_text) {
        return 0;
    }
    if (text_length > 0) {
        GetWindowTextW(state->edit_targets, archive_text, text_length + 1);
    }

    buffer_size = ((size_t)text_length + 1) * 12 + 128;
    json_buffer = (char*)calloc(buffer_size, sizeof(char));
    if (!json_buffer) {
        free(archive_text);
        return 0;
    }

    strcpy_s(json_buffer, buffer_size, "{\n  \"downloaded_links\": [\n");
    line = wcstok_s(archive_text, L"\r\n", &context);
    while (line) {
        char* item_utf8 = NULL;
        char escaped[4096];

        normalize_target_line(line);
        if (line[0] && line[0] != L'#') {
            item_utf8 = wide_to_utf8_alloc(line);
            if (item_utf8) {
                json_escape_string(item_utf8, escaped, sizeof(escaped));
                if (!first_item) {
                    strcat_s(json_buffer, buffer_size, ",\n");
                }
                strcat_s(json_buffer, buffer_size, "    \"");
                strcat_s(json_buffer, buffer_size, escaped);
                strcat_s(json_buffer, buffer_size, "\"");
                first_item = 0;
            }
            free(item_utf8);
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }
    strcat_s(json_buffer, buffer_size, "\n  ]\n}\n");

    safe_wcs_copy(archive_directory, PATH_BUFFER_COUNT, archive_path);
    PathRemoveFileSpecW(archive_directory);
    SHCreateDirectoryExW(NULL, archive_directory, NULL);

    if (!write_text_file_utf8(archive_path, json_buffer)) {
        free(json_buffer);
        free(archive_text);
        return 0;
    }

    free(json_buffer);
    free(archive_text);
    update_download_counts(state);
    return 1;
}

static void load_targets_list_to_ui(AppState* state)
{
    if (!state || !state->edit_targets) {
        return;
    }

    if (state->target_results_text && state->planned_download_total > 0) {
        SetWindowTextW(state->edit_targets, state->target_results_text);
    } else {
        wchar_t* targets_text = load_string_array_file_to_wide_lines(state->paths.targets_path, "targets");
        SetWindowTextW(state->edit_targets, targets_text ? targets_text : L"");
        free(targets_text);
    }
    update_download_counts(state);
}

static void load_archive_list_to_ui(AppState* state)
{
    wchar_t archive_path[PATH_BUFFER_COUNT];

    if (!state || !state->edit_targets) {
        return;
    }

    resolve_against_app_root(&state->paths, state->config.archive_file, archive_path, PATH_BUFFER_COUNT);
    {
        wchar_t* archive_text = load_string_array_file_to_wide_lines(archive_path, "downloaded_links");
        SetWindowTextW(state->edit_targets, archive_text ? archive_text : L"");
        free(archive_text);
    }
    update_download_counts(state);
}

static void refresh_archive_list_preserve_view(AppState* state)
{
    wchar_t archive_path[PATH_BUFFER_COUNT];
    wchar_t* archive_text = NULL;

    if (!state || !state->edit_targets) {
        return;
    }

    resolve_against_app_root(&state->paths, state->config.archive_file, archive_path, PATH_BUFFER_COUNT);
    archive_text = load_string_array_file_to_wide_lines(archive_path, "downloaded_links");
    set_edit_text_preserve_view(state->edit_targets, archive_text ? archive_text : L"");
    free(archive_text);
    update_download_counts(state);
}

static void update_list_view_ui(AppState* state)
{
    int busy = 0;

    if (!state) {
        return;
    }

    busy = state->planned_download_total > 0;

    if (state->label_targets) {
        switch (state->current_list_view) {
        case LIST_VIEW_TARGETS:
            SetWindowTextW(state->label_targets, state_text(state, L"label_targets_view_targets"));
            break;
        case LIST_VIEW_ARCHIVE:
            SetWindowTextW(state->label_targets, state_text(state, L"label_targets_view_archive"));
            break;
        default:
            SetWindowTextW(state->label_targets, state_text(state, L"label_targets_select_view"));
            break;
        }
    }

    if (state->button_view_targets) {
        InvalidateRect(state->button_view_targets, NULL, TRUE);
    }
    if (state->button_view_archive) {
        InvalidateRect(state->button_view_archive, NULL, TRUE);
    }
    if (state->button_delete_target) {
        EnableWindow(state->button_delete_target, !busy && state->current_list_view != LIST_VIEW_NONE);
    }
    if (state->button_delete_all) {
        EnableWindow(state->button_delete_all, !busy && state->current_list_view != LIST_VIEW_NONE);
    }
}

static void refresh_current_list_view(AppState* state)
{
    if (!state || !state->edit_targets) {
        return;
    }

    switch (state->current_list_view) {
    case LIST_VIEW_TARGETS:
        load_targets_list_to_ui(state);
        break;
    case LIST_VIEW_ARCHIVE:
        load_archive_list_to_ui(state);
        break;
    default:
        SetWindowTextW(state->edit_targets, L"");
        break;
    }

    update_list_view_ui(state);
}

static void set_current_list_view(AppState* state, int view)
{
    if (!state) {
        return;
    }

    state->current_list_view = view;
    refresh_current_list_view(state);
}

static int clear_current_list_view(AppState* state)
{
    if (!state || !state->edit_targets || state->current_list_view == LIST_VIEW_NONE) {
        return 0;
    }

    SetWindowTextW(state->edit_targets, L"");
    if (state->current_list_view == LIST_VIEW_ARCHIVE) {
        if (!save_archive_list_from_ui(state)) {
            append_log_line(state, L"[error] Could not save config/archive.json.");
            return 0;
        }
        append_log_line(state, state_text(state, L"log_cleared_archive"));
    } else {
        clear_target_results_text(state);
        if (!save_targets_list_from_ui(state)) {
            append_log_line(state, L"[error] Could not save config/list.json.");
            return 0;
        }
        append_log_line(state, state_text(state, L"log_cleared_targets"));
    }

    return 1;
}

static int read_clipboard_text(HWND window, wchar_t* output, size_t output_count)
{
    HANDLE clipboard_data = NULL;
    const wchar_t* clipboard_text = NULL;

    if (!output || output_count == 0) {
        return 0;
    }
    output[0] = L'\0';

    if (!OpenClipboard(window)) {
        return 0;
    }

    clipboard_data = GetClipboardData(CF_UNICODETEXT);
    if (!clipboard_data) {
        CloseClipboard();
        return 0;
    }

    clipboard_text = (const wchar_t*)GlobalLock(clipboard_data);
    if (!clipboard_text) {
        CloseClipboard();
        return 0;
    }

    safe_wcs_copy(output, output_count, clipboard_text);
    GlobalUnlock(clipboard_data);
    CloseClipboard();
    return 1;
}

static int looks_like_pixiv_clipboard_target(const wchar_t* text)
{
    if (!text || !text[0]) {
        return 0;
    }

    return ((StrStrIW(text, L"http://") || StrStrIW(text, L"https://"))
        && (StrStrIW(text, L"pixiv") || StrStrIW(text, L"pximg.net"))) ? 1 : 0;
}

static int looks_like_pixiv_auth_callback(const wchar_t* text)
{
    if (!text || !text[0]) {
        return 0;
    }

    return StrStrIW(text, L"app-api.pixiv.net/web/v1/users/auth/pixiv/callback") != NULL ? 1 : 0;
}

static int is_refresh_token_char(wchar_t ch)
{
    return ((ch >= L'0' && ch <= L'9')
        || (ch >= L'A' && ch <= L'Z')
        || (ch >= L'a' && ch <= L'z')
        || ch == L'_'
        || ch == L'-') ? 1 : 0;
}

static int copy_refresh_token_candidate(const wchar_t* start, wchar_t* output, size_t output_count)
{
    const wchar_t* p = start;
    size_t length = 0;

    if (!start || !output || output_count == 0) {
        return 0;
    }

    while (*p && is_refresh_token_char(*p) && length + 1 < output_count) {
        ++p;
        ++length;
    }
    if (length < 24) {
        return 0;
    }

    wcsncpy_s(output, output_count, start, length);
    return 1;
}

static int extract_refresh_token_from_text(const wchar_t* text, wchar_t* output, size_t output_count)
{
    static const wchar_t* patterns[] = {
        L"refresh_token",
        L"refresh-token",
        L"refresh token"
    };
    wchar_t trimmed[4096];
    const wchar_t* p = NULL;
    int index = 0;

    if (!output || output_count == 0) {
        return 0;
    }
    output[0] = L'\0';

    if (!text || !text[0]) {
        return 0;
    }

    safe_wcs_copy(trimmed, 4096, text);
    trim_wide_text(trimmed);
    if (!trimmed[0]) {
        return 0;
    }

    p = trimmed;
    while (*p) {
        if (!is_refresh_token_char(*p)) {
            break;
        }
        ++p;
    }
    if (*p == L'\0' && copy_refresh_token_candidate(trimmed, output, output_count)) {
        return 1;
    }

    for (index = 0; index < (int)(sizeof(patterns) / sizeof(patterns[0])); ++index) {
        const wchar_t* found = StrStrIW(trimmed, patterns[index]);
        while (found) {
            const wchar_t* cursor = found + wcslen(patterns[index]);

            while (*cursor && *cursor != L':' && *cursor != L'=') {
                if (!iswspace(*cursor) && *cursor != L'"' && *cursor != L'\'') {
                    break;
                }
                ++cursor;
            }
            if (*cursor == L':' || *cursor == L'=') {
                wchar_t quote = L'\0';
                ++cursor;
                while (*cursor && iswspace(*cursor)) {
                    ++cursor;
                }
                if (*cursor == L'"' || *cursor == L'\'') {
                    quote = *cursor;
                    ++cursor;
                }

                if (copy_refresh_token_candidate(cursor, output, output_count)) {
                    if (!quote || wcschr(cursor, quote) != NULL) {
                        return 1;
                    }
                }
            }

            found = StrStrIW(found + 1, patterns[index]);
        }
    }

    return 0;
}

static int is_auth_code_char(wchar_t ch)
{
    return ((ch >= L'0' && ch <= L'9')
        || (ch >= L'A' && ch <= L'Z')
        || (ch >= L'a' && ch <= L'z')
        || ch == L'_'
        || ch == L'-'
        || ch == L'.'
        || ch == L'~') ? 1 : 0;
}

static int copy_auth_code_candidate(const wchar_t* start, wchar_t* output, size_t output_count)
{
    const wchar_t* p = start;
    size_t length = 0;

    if (!start || !output || output_count == 0) {
        return 0;
    }

    while (*p && *p != L'&' && *p != L'#' && *p != L'?' && *p != L'"' && *p != L'\''
        && *p != L' ' && *p != L'\r' && *p != L'\n' && *p != L'\t') {
        if (!is_auth_code_char(*p)) {
            break;
        }
        if (length + 1 >= output_count) {
            break;
        }
        output[length++] = *p;
        ++p;
    }

    output[length] = L'\0';
    return length >= 8 ? 1 : 0;
}

static int extract_authorization_code_from_text(const wchar_t* text, wchar_t* output, size_t output_count)
{
    static const wchar_t* patterns[] = {
        L"code=",
        L"\"code\":",
        L"'code':",
        L"callback?state=",
    };
    wchar_t trimmed[4096];
    int index = 0;

    if (!output || output_count == 0) {
        return 0;
    }
    output[0] = L'\0';

    if (!text || !text[0]) {
        return 0;
    }

    safe_wcs_copy(trimmed, 4096, text);
    trim_wide_text(trimmed);
    if (!trimmed[0]) {
        return 0;
    }

    for (index = 0; index < (int)(sizeof(patterns) / sizeof(patterns[0])); ++index) {
        const wchar_t* found = StrStrIW(trimmed, patterns[index]);

        while (found) {
            const wchar_t* cursor = found;

            if (_wcsicmp(patterns[index], L"callback?state=") == 0) {
                const wchar_t* code_marker = StrStrIW(found, L"code=");
                if (code_marker) {
                    cursor = code_marker + 5;
                    if (copy_auth_code_candidate(cursor, output, output_count)) {
                        return 1;
                    }
                }
            } else {
                cursor += wcslen(patterns[index]);
                while (*cursor && iswspace(*cursor)) {
                    ++cursor;
                }
                if (*cursor == L'"' || *cursor == L'\'') {
                    ++cursor;
                }
                if (copy_auth_code_candidate(cursor, output, output_count)) {
                    return 1;
                }
            }

            found = StrStrIW(found + 1, patterns[index]);
        }
    }

    if (wcslen(trimmed) >= 20 && !wcschr(trimmed, L'/') && !wcschr(trimmed, L':')
        && copy_auth_code_candidate(trimmed, output, output_count)) {
        return 1;
    }

    return 0;
}

static int target_exists_in_text(const wchar_t* text, const wchar_t* target)
{
    wchar_t* buffer = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    wchar_t normalized_target[4096];
    wchar_t canonical_target[4096];
    int found = 0;

    if (!text || !target || !target[0]) {
        return 0;
    }

    safe_wcs_copy(normalized_target, _countof(normalized_target), target);
    normalize_target_line(normalized_target);
    canonicalize_pixiv_target_copy(normalized_target, canonical_target, _countof(canonical_target));
    if (!canonical_target[0]) {
        return 0;
    }

    buffer = (wchar_t*)calloc(wcslen(text) + 1, sizeof(wchar_t));
    if (!buffer) {
        return 0;
    }
    wcscpy_s(buffer, wcslen(text) + 1, text);

    line = wcstok_s(buffer, L"\r\n", &context);
    while (line) {
        wchar_t canonical_line[4096];

        normalize_target_line(line);
        canonicalize_pixiv_target_copy(line, canonical_line, _countof(canonical_line));
        if (canonical_line[0] && _wcsicmp(canonical_line, canonical_target) == 0) {
            found = 1;
            break;
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }

    free(buffer);
    return found;
}

static wchar_t* append_unique_line_alloc(const wchar_t* current_text, const wchar_t* target, int* added)
{
    wchar_t* working = NULL;
    wchar_t* combined = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    size_t capacity = 0;
    size_t used = 0;
    size_t current_length = current_text ? wcslen(current_text) : 0;
    wchar_t normalized_target[4096];
    wchar_t canonical_target[4096];

    if (added) {
        *added = 0;
    }
    if (!target || !target[0]) {
        return NULL;
    }
    safe_wcs_copy(normalized_target, _countof(normalized_target), target);
    normalize_target_line(normalized_target);
    canonicalize_pixiv_target_copy(normalized_target, canonical_target, _countof(canonical_target));
    if (!canonical_target[0]) {
        return NULL;
    }
    if (current_text && current_text[0] && target_exists_in_text(current_text, canonical_target)) {
        return NULL;
    }

    capacity = current_length + wcslen(canonical_target) + 64;
    combined = (wchar_t*)calloc(capacity, sizeof(wchar_t));
    if (!combined) {
        return NULL;
    }

    if (current_text && current_text[0]) {
        working = (wchar_t*)calloc(current_length + 1, sizeof(wchar_t));
        if (!working) {
            free(combined);
            return NULL;
        }
        wcscpy_s(working, current_length + 1, current_text);
        line = wcstok_s(working, L"\r\n", &context);
        while (line) {
            trim_wide_text(line);
            if (line[0]) {
                if (!append_line_to_wide_buffer(&combined, &capacity, &used, line)) {
                    free(working);
                    free(combined);
                    return NULL;
                }
            }
            line = wcstok_s(NULL, L"\r\n", &context);
        }
        free(working);
    }

    if (!append_line_to_wide_buffer(&combined, &capacity, &used, canonical_target)) {
        free(combined);
        return NULL;
    }

    if (added) {
        *added = 1;
    }
    return combined;
}

static int append_unique_line_inplace(wchar_t** buffer, const wchar_t* target, int* added)
{
    wchar_t* combined = NULL;

    if (added) {
        *added = 0;
    }
    if (!buffer || !target || !target[0]) {
        return 0;
    }

    combined = append_unique_line_alloc(*buffer ? *buffer : L"", target, added);
    if (combined) {
        free(*buffer);
        *buffer = combined;
        return 1;
    }

    if (*buffer && target_exists_in_text(*buffer, target)) {
        return 1;
    }
    if (!*buffer) {
        combined = append_unique_line_alloc(L"", target, added);
        if (combined) {
            *buffer = combined;
            return 1;
        }
    }
    return 0;
}

static int build_expanded_download_queue(
    const AppConfig* config,
    const wchar_t* raw_targets_text,
    wchar_t* shared_access_token,
    size_t shared_access_token_count,
    wchar_t** expanded_targets_out,
    int* expanded_target_count_out
)
{
    wchar_t* working = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    wchar_t* expanded_targets = NULL;
    int expanded_count = 0;

    if (expanded_targets_out) {
        *expanded_targets_out = NULL;
    }
    if (expanded_target_count_out) {
        *expanded_target_count_out = 0;
    }
    if (!config || !raw_targets_text) {
        return 0;
    }

    working = _wcsdup(raw_targets_text);
    if (!working) {
        return 0;
    }

    line = wcstok_s(working, L"\r\n", &context);
    while (line) {
        wchar_t normalized_target[4096];
        wchar_t error_message[512];
        wchar_t* expanded_lines = NULL;
        wchar_t* expanded_context = NULL;
        wchar_t* expanded_line = NULL;
        int resolved_count = 0;

        safe_wcs_copy(normalized_target, _countof(normalized_target), line);
        normalize_target_line(normalized_target);
        canonicalize_pixiv_target_copy(normalized_target, normalized_target, _countof(normalized_target));
        if (!normalized_target[0] || normalized_target[0] == L'#') {
            line = wcstok_s(NULL, L"\r\n", &context);
            continue;
        }

        if (!pixiv_expand_target_to_artwork_urls(
                config,
                shared_access_token,
                shared_access_token_count,
                normalized_target,
                &expanded_lines,
                &resolved_count,
                error_message,
                _countof(error_message))
            || resolved_count <= 0) {
            if (StrStrIW(normalized_target, L"pixiv.net/users/")) {
                free(expanded_lines);
                free(expanded_targets);
                free(working);
                return 0;
            }
            if (append_unique_line_inplace(&expanded_targets, normalized_target, &resolved_count) && resolved_count > 0) {
                ++expanded_count;
            }
            line = wcstok_s(NULL, L"\r\n", &context);
            continue;
        }

        expanded_line = wcstok_s(expanded_lines, L"\r\n", &expanded_context);
        while (expanded_line) {
            int added = 0;
            normalize_target_line(expanded_line);
            if (expanded_line[0] && append_unique_line_inplace(&expanded_targets, expanded_line, &added) && added > 0) {
                ++expanded_count;
            }
            expanded_line = wcstok_s(NULL, L"\r\n", &expanded_context);
        }

        free(expanded_lines);
        line = wcstok_s(NULL, L"\r\n", &context);
    }

    free(working);

    if (!expanded_targets || expanded_count <= 0) {
        free(expanded_targets);
        return 0;
    }

    if (expanded_targets_out) {
        *expanded_targets_out = expanded_targets;
    } else {
        free(expanded_targets);
    }
    if (expanded_target_count_out) {
        *expanded_target_count_out = expanded_count;
    }
    return 1;
}

static int write_wide_lines_to_json_array_file(const wchar_t* file_path, const wchar_t* lines_text, const char* key_name)
{
    wchar_t* text_copy = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    size_t text_length = 0;
    size_t buffer_size = 0;
    char* json_buffer = NULL;
    wchar_t directory[PATH_BUFFER_COUNT];
    int first_item = 1;

    if (!file_path || !file_path[0] || !key_name || !key_name[0]) {
        return 0;
    }

    text_length = lines_text ? wcslen(lines_text) : 0;
    text_copy = (wchar_t*)calloc(text_length + 1, sizeof(wchar_t));
    if (!text_copy) {
        return 0;
    }
    if (text_length > 0) {
        wcscpy_s(text_copy, text_length + 1, lines_text);
    }

    buffer_size = (text_length + 1) * 12 + 128;
    json_buffer = (char*)calloc(buffer_size, sizeof(char));
    if (!json_buffer) {
        free(text_copy);
        return 0;
    }

    sprintf_s(json_buffer, buffer_size, "{\n  \"%s\": [\n", key_name);
    line = wcstok_s(text_copy, L"\r\n", &context);
    while (line) {
        char* item_utf8 = NULL;
        char escaped[4096];

        normalize_target_line(line);
        if (line[0] && line[0] != L'#') {
            item_utf8 = wide_to_utf8_alloc(line);
            if (item_utf8) {
                json_escape_string(item_utf8, escaped, sizeof(escaped));
                if (!first_item) {
                    strcat_s(json_buffer, buffer_size, ",\n");
                }
                strcat_s(json_buffer, buffer_size, "    \"");
                strcat_s(json_buffer, buffer_size, escaped);
                strcat_s(json_buffer, buffer_size, "\"");
                first_item = 0;
            }
            free(item_utf8);
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }
    strcat_s(json_buffer, buffer_size, "\n  ]\n}\n");

    safe_wcs_copy(directory, PATH_BUFFER_COUNT, file_path);
    PathRemoveFileSpecW(directory);
    SHCreateDirectoryExW(NULL, directory, NULL);

    if (!write_text_file_utf8(file_path, json_buffer)) {
        free(json_buffer);
        free(text_copy);
        return 0;
    }

    free(json_buffer);
    free(text_copy);
    return 1;
}

static int append_target_to_targets_storage(AppState* state, const wchar_t* target, int update_view)
{
    wchar_t* source_text = NULL;
    wchar_t* combined = NULL;
    int added = 0;
    int success = 0;

    if (!state || !target || !target[0]) {
        return 0;
    }

    if (state->planned_download_total <= 0
        && state->current_list_view == LIST_VIEW_TARGETS
        && state->edit_targets) {
        int text_length = GetWindowTextLengthW(state->edit_targets);
        source_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
        if (!source_text) {
            return 0;
        }
        if (text_length > 0) {
            GetWindowTextW(state->edit_targets, source_text, text_length + 1);
        }
    } else {
        source_text = load_string_array_file_to_wide_lines(state->paths.targets_path, "targets");
    }

    combined = append_unique_line_alloc(source_text ? source_text : L"", target, &added);
    if (!combined) {
        free(source_text);
        return 0;
    }

    if (!write_wide_lines_to_json_array_file(state->paths.targets_path, combined, "targets")) {
        append_log_line(state, L"[error] Could not save config/list.json.");
        free(combined);
        free(source_text);
        return 0;
    }

    if (update_view
        && state->planned_download_total <= 0
        && state->current_list_view == LIST_VIEW_TARGETS) {
        set_target_results_text(state, combined);
        set_edit_text_preserve_view(state->edit_targets, combined);
    }

    success = added;
    free(combined);
    free(source_text);
    update_download_counts(state);
    return success;
}

static void mark_target_result_in_edit(AppState* state, const wchar_t* target, int success)
{
    int text_length = 0;
    wchar_t* source_text = NULL;
    wchar_t* combined = NULL;
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    size_t capacity = 0;
    size_t used = 0;

    if (!state || !target || !target[0]) {
        return;
    }

    if (state->target_results_text) {
        text_length = (int)wcslen(state->target_results_text);
        source_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
        if (!source_text) {
            return;
        }
        if (text_length > 0) {
            wcscpy_s(source_text, (size_t)text_length + 1, state->target_results_text);
        }
    } else if (state->edit_targets) {
        text_length = GetWindowTextLengthW(state->edit_targets);
        if (text_length > 0) {
            source_text = (wchar_t*)calloc((size_t)text_length + 1, sizeof(wchar_t));
            if (!source_text) {
                return;
            }
            GetWindowTextW(state->edit_targets, source_text, text_length + 1);
        }
    }

    if (!source_text || text_length <= 0) {
        free(source_text);
        return;
    }

    capacity = ((size_t)text_length + 64) * 2;
    combined = (wchar_t*)calloc(capacity, sizeof(wchar_t));
    if (!combined) {
        free(source_text);
        return;
    }

    line = wcstok_s(source_text, L"\r\n", &context);
    while (line) {
        wchar_t display_line[4096];
        wchar_t normalized_line[4096];

        safe_wcs_copy(display_line, 4096, line);
        trim_wide_text(display_line);
        safe_wcs_copy(normalized_line, 4096, display_line);
        normalize_target_line(normalized_line);

        if (normalized_line[0]) {
            if (_wcsicmp(normalized_line, target) == 0) {
                swprintf_s(display_line, 4096, success ? L"%ls \x2705" : L"%ls \x274C", normalized_line);
            }
            if (!append_line_to_wide_buffer(&combined, &capacity, &used, display_line)) {
                free(combined);
                free(source_text);
                return;
            }
        }

        line = wcstok_s(NULL, L"\r\n", &context);
    }

    set_target_results_text(state, combined);
    if (state->current_list_view == LIST_VIEW_TARGETS && state->edit_targets) {
        set_edit_text_preserve_view(state->edit_targets, combined);
    }
    free(combined);
    free(source_text);
}

static void update_auto_clipboard_ui(AppState* state)
{
    if (!state || !state->button_auto_clipboard) {
        return;
    }

    SetWindowTextW(
        state->button_auto_clipboard,
        state->auto_clipboard_enabled
            ? state_text(state, L"button_auto_clipboard_on")
            : state_text(state, L"button_auto_clipboard_off")
    );
}

static void update_duplicate_download_ui(AppState* state)
{
    if (!state || !state->button_duplicate_download) {
        return;
    }

    SetWindowTextW(
        state->button_duplicate_download,
        state->allow_duplicate_downloads
            ? state_text(state, L"button_duplicate_download_on")
            : state_text(state, L"button_duplicate_download_off")
    );
}

static void sync_clipboard_snapshot(AppState* state)
{
    wchar_t clipboard_text[4096];

    if (!state) {
        return;
    }

    if (read_clipboard_text(state->window, clipboard_text, 4096)) {
        safe_wcs_copy(state->last_clipboard_text, 4096, clipboard_text);
    } else {
        state->last_clipboard_text[0] = L'\0';
    }
}

static void toggle_auto_clipboard(AppState* state)
{
    wchar_t message[1024];

    if (!state) {
        return;
    }

    state->auto_clipboard_enabled = !state->auto_clipboard_enabled;
    update_auto_clipboard_ui(state);
    sync_clipboard_snapshot(state);

    save_config_from_ui(state, 0);

    if (state->auto_clipboard_enabled) {
        format_state_text_one(state, L"log_auto_clipboard_enabled", L"{path}", state->paths.targets_path, message, 1024);
    } else {
        safe_wcs_copy(message, 1024, state_text(state, L"log_auto_clipboard_disabled"));
    }
    append_log_line(state, message);
}

static void toggle_duplicate_download(AppState* state)
{
    if (!state) {
        return;
    }

    state->allow_duplicate_downloads = !state->allow_duplicate_downloads;
    update_duplicate_download_ui(state);
    save_config_from_ui(state, 0);

    if (state->allow_duplicate_downloads) {
        append_log_line(state, state_text(state, L"log_duplicate_download_enabled"));
    } else {
        append_log_line(state, state_text(state, L"log_duplicate_download_disabled"));
    }
}

static void handle_clipboard_update(AppState* state)
{
    wchar_t clipboard_text[4096];
    wchar_t buffer[4096];
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    const wchar_t* file_name = NULL;
    int play_siren = 0;

    if (!state) {
        return;
    }
    if (!read_clipboard_text(state->window, clipboard_text, 4096)) {
        return;
    }
    if (_wcsicmp(clipboard_text, state->last_clipboard_text) == 0) {
        return;
    }

    safe_wcs_copy(state->last_clipboard_text, 4096, clipboard_text);
    import_refresh_token_from_text(state, clipboard_text, 1, 1);
    import_authorization_code_from_text(state, clipboard_text, 1, 1);

    if (!state->auto_clipboard_enabled) {
        return;
    }

    safe_wcs_copy(buffer, 4096, clipboard_text);
    file_name = PathFindFileNameW(state->paths.targets_path);

    line = wcstok_s(buffer, L"\r\n", &context);
    while (line) {
        wchar_t message[2048];

        normalize_target_line(line);
        if (!looks_like_pixiv_auth_callback(line) && looks_like_pixiv_clipboard_target(line)) {
            if (append_target_to_targets_storage(state, line, state->current_list_view == LIST_VIEW_TARGETS)) {
                play_siren = 1;
                format_state_text_two(
                    state,
                    L"log_added_to_targets",
                    L"{filename}",
                    file_name && file_name[0] ? file_name : L"list.json",
                    L"{target}",
                    line,
                    message,
                    2048
                );
                append_log_line(state, message);
            }
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }

    if (play_siren) {
        play_auto_clipboard_siren(state);
    }
}

static void mask_token_for_log(const wchar_t* token, wchar_t* output, size_t output_count)
{
    size_t length = 0;
    if (!output || output_count == 0) {
        return;
    }
    output[0] = L'\0';

    if (!token || token[0] == L'\0') {
        safe_wcs_copy(output, output_count, L"(not set)");
        return;
    }

    length = wcslen(token);
    if (length <= 10) {
        size_t stars = length > 2 ? length - 2 : 0;
        safe_wcs_copy(output, output_count, token);
        for (size_t index = 2; index < 2 + stars && index < output_count - 1; ++index) {
            output[index] = L'*';
        }
        return;
    }

    swprintf_s(output, output_count, L"%.4ls...%.4ls", token, token + length - 4);
}

static void set_status_text(AppState* state, const wchar_t* text)
{
    if (!state || !state->window) {
        return;
    }
    SendMessageW(state->window, WM_SETTEXT, 0, (LPARAM)text);
}

static void update_ffmpeg_status(AppState* state)
{
    wchar_t configured[PATH_BUFFER_COUNT];
    wchar_t detected[PATH_BUFFER_COUNT];

    if (!state || !state->edit_ffmpeg || !state->edit_ffmpeg_status) {
        return;
    }

    GetWindowTextW(state->edit_ffmpeg, configured, PATH_BUFFER_COUNT);

    if (detect_ffmpeg_path(&state->paths, configured, detected, PATH_BUFFER_COUNT)) {
        SetWindowTextW(state->edit_ffmpeg_status, detected);
    } else {
        SetWindowTextW(state->edit_ffmpeg_status, state_text(state, L"status_ffmpeg_not_found"));
    }
}

static void populate_ugoira_combo(HWND combo)
{
    const wchar_t* values[] = { L"copy", L"gif", L"mp4", L"skip", L"vp8", L"vp9", L"vp9-lossless", L"webm", L"zip" };
    int index = 0;
    for (index = 0; index < (int)(sizeof(values) / sizeof(values[0])); ++index) {
        ComboBox_AddString(combo, values[index]);
    }
}

static void populate_language_combo(AppState* state)
{
    int index = 0;
    if (!state || !state->combo_language) {
        return;
    }
    state->language_count = locale_list_languages(&state->paths, state->languages, MAX_LANGUAGE_ITEMS);
    ComboBox_ResetContent(state->combo_language);

    for (index = 0; index < state->language_count; ++index) {
        int combo_index = ComboBox_AddString(state->combo_language, state->languages[index].display_name);
        ComboBox_SetItemData(state->combo_language, combo_index, index);
    }
}

static void load_controls_from_config(AppState* state)
{
    int index = 0;
    wchar_t number_buffer[16];
    state->auto_clipboard_enabled = state->config.auto_clipboard_to_targets ? 1 : 0;
    state->allow_duplicate_downloads = state->config.allow_duplicate_downloads ? 1 : 0;
    Button_SetCheck(state->check_illustrations, state->config.download_illustrations ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(state->check_manga, state->config.download_manga ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(state->check_novels, state->config.download_novels ? BST_CHECKED : BST_UNCHECKED);

    if (settings_controls_available(state)) {
        SetWindowTextW(state->edit_token, state->config.refresh_token);
        SetWindowTextW(state->edit_ffmpeg, state->config.ffmpeg_path);
        SetWindowTextW(state->edit_sleep, state->config.sleep_request);
        SetWindowTextW(state->edit_parallel, L"3");
        if (state->config.parallel_downloads > 0) {
            swprintf_s(number_buffer, 16, L"%d", state->config.parallel_downloads);
            SetWindowTextW(state->edit_parallel, number_buffer);
        }
        swprintf_s(number_buffer, 16, L"%d", state->config.profile_page_size > 0 ? state->config.profile_page_size : 32);
        SetWindowTextW(state->edit_profile_page_size, number_buffer);

        SetWindowTextW(state->edit_artist_folder, state->config.artist_folder_format);
        SetWindowTextW(state->edit_multi_folder, state->config.multi_image_folder_format);
        SetWindowTextW(state->edit_single_file, state->config.single_image_filename_format);
        SetWindowTextW(state->edit_multi_file, state->config.multi_image_filename_format);
        SetWindowTextW(state->edit_download_dir, state->config.download_dir);
        SetWindowTextW(state->edit_archive_file, state->config.archive_file);
        Button_SetCheck(state->check_metadata, state->config.write_metadata ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(state->check_auto_update, state->config.auto_update ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(state->edit_github_repository, state->config.github_repository);

        ComboBox_SelectString(state->combo_ugoira, -1, state->config.ugoira_format);
        for (index = 0; index < state->language_count; ++index) {
            if (_wcsicmp(state->languages[index].code, state->config.language) == 0) {
                ComboBox_SetCurSel(state->combo_language, index);
                break;
            }
        }
        update_ffmpeg_status(state);
    }

    update_auto_clipboard_ui(state);
    update_duplicate_download_ui(state);
    sync_clipboard_snapshot(state);
    update_download_counts(state);
}

static void collect_config_from_controls(AppState* state)
{
    wchar_t parallel_buffer[16];
    wchar_t profile_page_size_buffer[16];
    int language_index = 0;
    state->config.auto_clipboard_to_targets = state->auto_clipboard_enabled ? 1 : 0;
    state->config.allow_duplicate_downloads = state->allow_duplicate_downloads ? 1 : 0;
    state->config.download_illustrations = Button_GetCheck(state->check_illustrations) == BST_CHECKED;
    state->config.download_manga = Button_GetCheck(state->check_manga) == BST_CHECKED;
    state->config.download_novels = Button_GetCheck(state->check_novels) == BST_CHECKED;

    if (!settings_controls_available(state)) {
        return;
    }

    GetWindowTextW(state->edit_token, state->config.refresh_token, 512);
    GetWindowTextW(state->edit_ffmpeg, state->config.ffmpeg_path, PATH_BUFFER_COUNT);
    GetWindowTextW(state->edit_sleep, state->config.sleep_request, 64);
    GetWindowTextW(state->edit_parallel, parallel_buffer, 16);
    state->config.parallel_downloads = _wtoi(parallel_buffer);
    if (state->config.parallel_downloads < MIN_PARALLEL_DOWNLOADS) {
        state->config.parallel_downloads = MIN_PARALLEL_DOWNLOADS;
    } else if (state->config.parallel_downloads > MAX_PARALLEL_DOWNLOADS) {
        state->config.parallel_downloads = MAX_PARALLEL_DOWNLOADS;
    }
    swprintf_s(parallel_buffer, _countof(parallel_buffer), L"%d", state->config.parallel_downloads);
    SetWindowTextW(state->edit_parallel, parallel_buffer);
    GetWindowTextW(state->edit_profile_page_size, profile_page_size_buffer, 16);
    state->config.profile_page_size = _wtoi(profile_page_size_buffer);
    if (state->config.profile_page_size < 1) {
        state->config.profile_page_size = 32;
    }

    GetWindowTextW(state->edit_artist_folder, state->config.artist_folder_format, 256);
    GetWindowTextW(state->edit_multi_folder, state->config.multi_image_folder_format, 256);
    GetWindowTextW(state->edit_single_file, state->config.single_image_filename_format, 256);
    GetWindowTextW(state->edit_multi_file, state->config.multi_image_filename_format, 256);
    GetWindowTextW(state->edit_download_dir, state->config.download_dir, PATH_BUFFER_COUNT);
    GetWindowTextW(state->edit_archive_file, state->config.archive_file, PATH_BUFFER_COUNT);
    state->config.write_metadata = Button_GetCheck(state->check_metadata) == BST_CHECKED;
    state->config.auto_update = Button_GetCheck(state->check_auto_update) == BST_CHECKED;
    GetWindowTextW(state->edit_github_repository, state->config.github_repository, 2048);

    GetWindowTextW(state->combo_ugoira, state->config.ugoira_format, 32);

    language_index = ComboBox_GetCurSel(state->combo_language);
    if (language_index >= 0 && language_index < state->language_count) {
        safe_wcs_copy(state->config.language, 32, state->languages[language_index].code);
    }
}

static void show_message(HWND window, const wchar_t* title, const wchar_t* body, UINT flags)
{
    MessageBoxW(window, body, title, flags);
}

static int save_config_silent(AppState* state)
{
    wchar_t error_message[256];

    if (!state) {
        return 0;
    }

    collect_config_from_controls(state);
    if (!config_save(&state->paths, &state->config, error_message, 256)) {
        append_log(state, L"[error] ");
        append_log_line(state, error_message);
        return 0;
    }

    safe_wcs_copy(state->loaded_language, 32, state->config.language);
    if (settings_controls_available(state)) {
        update_ffmpeg_status(state);
    }
    return 1;
}

static DWORD WINAPI update_check_thread(LPVOID parameter)
{
    UpdateCheckContext* context = (UpdateCheckContext*)parameter;
    UpdateCheckMessage* message = NULL;

    if (!context) {
        return 1;
    }
    message = (UpdateCheckMessage*)calloc(1, sizeof(UpdateCheckMessage));
    if (!message) {
        free(context);
        return 1;
    }

    message->manual = context->manual;
    message->succeeded = updater_check_github_release(
        context->repository,
        YN_APP_VERSION,
        &message->manifest,
        &message->update_available,
        message->error_message,
        _countof(message->error_message)
    );
    if (!PostMessageW(context->window, WM_APP_UPDATE_CHECKED, 0, (LPARAM)message)) {
        free(message);
    }
    free(context);
    return 0;
}

static DWORD WINAPI update_download_thread(LPVOID parameter)
{
    UpdateDownloadContext* context = (UpdateDownloadContext*)parameter;
    UpdateDownloadMessage* message = NULL;

    if (!context) {
        return 1;
    }
    message = (UpdateDownloadMessage*)calloc(1, sizeof(UpdateDownloadMessage));
    if (!message) {
        free(context);
        return 1;
    }

    message->succeeded = updater_download_and_verify(
        &context->manifest,
        message->staged_path,
        _countof(message->staged_path),
        message->error_message,
        _countof(message->error_message)
    );
    if (!PostMessageW(context->window, WM_APP_UPDATE_DOWNLOADED, 0, (LPARAM)message)) {
        if (message->staged_path[0]) {
            DeleteFileW(message->staged_path);
        }
        free(message);
    }
    free(context);
    return 0;
}

static void start_update_check(AppState* state, int manual)
{
    UpdateCheckContext* context = NULL;
    HANDLE thread = NULL;

    if (!state || state->update_check_in_progress || state->update_download_in_progress) {
        return;
    }
    if (manual && settings_controls_available(state)) {
        save_config_silent(state);
    }
    if (!state->config.github_repository[0]) {
        if (manual) {
            show_message(
                state->settings_window ? state->settings_window : state->window,
                state_text(state, L"dialog_update_title"),
                state_text(state, L"dialog_update_manifest_required"),
                MB_ICONWARNING
            );
        }
        return;
    }

    context = (UpdateCheckContext*)calloc(1, sizeof(UpdateCheckContext));
    if (!context) {
        return;
    }
    context->window = state->window;
    context->manual = manual;
    safe_wcs_copy(context->repository, _countof(context->repository), state->config.github_repository);

    state->update_check_in_progress = 1;
    if (state->button_check_update) {
        EnableWindow(state->button_check_update, FALSE);
    }
    append_log_line(state, state_text(state, L"log_update_checking"));
    thread = CreateThread(NULL, 0, update_check_thread, context, 0, NULL);
    if (!thread) {
        state->update_check_in_progress = 0;
        if (state->button_check_update) {
            EnableWindow(state->button_check_update, TRUE);
        }
        free(context);
        if (manual) {
            show_message(
                state->settings_window ? state->settings_window : state->window,
                state_text(state, L"dialog_update_title"),
                state_text(state, L"dialog_update_check_failed"),
                MB_ICONERROR
            );
        }
        return;
    }
    CloseHandle(thread);
}

static void start_update_download(AppState* state, const UpdateManifest* manifest)
{
    UpdateDownloadContext* context = NULL;
    HANDLE thread = NULL;

    if (!state || !manifest || state->update_download_in_progress) {
        return;
    }
    context = (UpdateDownloadContext*)calloc(1, sizeof(UpdateDownloadContext));
    if (!context) {
        return;
    }
    context->window = state->window;
    context->manifest = *manifest;
    state->update_download_in_progress = 1;
    append_log_line(state, state_text(state, L"log_update_downloading"));
    thread = CreateThread(NULL, 0, update_download_thread, context, 0, NULL);
    if (!thread) {
        state->update_download_in_progress = 0;
        free(context);
        show_message(
            state->window,
            state_text(state, L"dialog_update_title"),
            state_text(state, L"dialog_update_download_failed"),
            MB_ICONERROR
        );
        return;
    }
    CloseHandle(thread);
}

static int import_refresh_token_into_ui(AppState* state, const wchar_t* token, int save_silently, int write_log)
{
    wchar_t current_token[512];
    wchar_t masked_token[64];
    wchar_t error_message[256];

    if (!state || !token || !token[0]) {
        return 0;
    }

    if (!settings_controls_available(state)) {
        safe_wcs_copy(state->config.refresh_token, 512, token);
        mask_token_for_log(token, masked_token, 64);
        if (save_silently) {
            config_save(&state->paths, &state->config, error_message, 256);
        }
        if (write_log) {
            append_log(state, L"[native] Imported Pixiv refresh token: ");
            append_log_line(state, masked_token);
        }
        return 1;
    }

    GetWindowTextW(state->edit_token, current_token, 512);
    if (wcscmp(current_token, token) == 0) {
        return 1;
    }

    SetWindowTextW(state->edit_token, token);
    mask_token_for_log(token, masked_token, 64);

    if (save_silently) {
        save_config_silent(state);
    }

    if (write_log) {
        append_log(state, L"[native] Imported Pixiv refresh token: ");
        append_log_line(state, masked_token);
    }

    return 1;
}

static int import_refresh_token_from_text(AppState* state, const wchar_t* text, int save_silently, int write_log)
{
    wchar_t token[512];

    if (!extract_refresh_token_from_text(text, token, 512)) {
        return 0;
    }

    return import_refresh_token_into_ui(state, token, save_silently, write_log);
}

static int import_authorization_code_from_text(AppState* state, const wchar_t* text, int save_silently, int write_log)
{
    wchar_t auth_code[512];
    wchar_t refresh_token[512];
    wchar_t error_message[512];

    if (!state || !state->auth_code_waiting || !state->auth_code_verifier[0]) {
        return 0;
    }
    if (!extract_authorization_code_from_text(text, auth_code, 512)) {
        return 0;
    }

    if (!pixiv_exchange_auth_code_for_refresh_token(
        auth_code,
        state->auth_code_verifier,
        refresh_token,
        512,
        error_message,
        512
    )) {
        if (write_log) {
            append_log(state, L"[error] ");
            append_log_line(state, error_message);
        }
        return 0;
    }

    state->auth_code_verifier[0] = L'\0';
    state->auth_code_waiting = 0;
    if (write_log) {
        append_log_line(state, L"[native] Pixiv login completed. The refresh token was imported automatically.");
    }
    return import_refresh_token_into_ui(state, refresh_token, save_silently, write_log);
}

static void save_config_from_ui(AppState* state, int show_language_message)
{
    wchar_t error_message[256];
    wchar_t previous_language[32];

    safe_wcs_copy(previous_language, 32, state->loaded_language);
    collect_config_from_controls(state);

    if (!config_save(&state->paths, &state->config, error_message, 256)) {
        show_message(state->window, state_text(state, L"dialog_run_error_title"), error_message, MB_ICONERROR);
        return;
    }

    append_log(state, state_text(state, L"log_saved_config"));
    append_log(state, L"\r\n");
    if (state->current_list_view == LIST_VIEW_TARGETS && !save_targets_list_from_ui(state)) {
        append_log_line(state, L"[error] Could not save config/list.json.");
    }
    update_ffmpeg_status(state);
    update_download_counts(state);

    if (show_language_message && _wcsicmp(previous_language, state->config.language) != 0) {
        show_message(state->window, state_text(state, L"dialog_language_changed_title"), state_text(state, L"dialog_language_changed_body"), MB_ICONINFORMATION);
    }
    safe_wcs_copy(state->loaded_language, 32, state->config.language);
}

static void reload_config_to_ui(AppState* state)
{
    wchar_t error_message[256];
    if (!config_load(&state->paths, &state->config, error_message, 256)) {
        show_message(state->window, state_text(state, L"dialog_run_error_title"), error_message, MB_ICONERROR);
        return;
    }
    load_controls_from_config(state);
    refresh_current_list_view(state);
    append_log(state, state_text(state, L"log_loaded_config"));
    append_log(state, L"\r\n");
    update_download_counts(state);
}

static int has_download_targets(AppState* state)
{
    wchar_t input_file[PATH_BUFFER_COUNT];
    int targets_length = GetWindowTextLengthW(state->edit_targets);
    GetWindowTextW(state->edit_input_file, input_file, PATH_BUFFER_COUNT);
    normalize_target_line(input_file);
    return targets_length > 0 || input_file[0] != L'\0';
}

static void set_busy_state(AppState* state, int busy)
{
    EnableWindow(state->button_auto_clipboard, !busy);
    EnableWindow(state->button_duplicate_download, !busy);
    EnableWindow(state->button_view_targets, TRUE);
    EnableWindow(state->button_view_archive, TRUE);
    EnableWindow(state->button_add_target, !busy);
    EnableWindow(state->button_start, !busy);
    EnableWindow(state->button_stop, busy && !state->download_stop_requested);
    EnableWindow(state->edit_targets, TRUE);
    EnableWindow(state->edit_input_file, !busy);
    EnableWindow(state->check_dry_run, !busy);
    EnableWindow(state->check_verbose, !busy);
    EnableWindow(state->check_print_command, !busy);
    EnableWindow(state->check_illustrations, !busy);
    EnableWindow(state->check_manga, !busy);
    EnableWindow(state->check_novels, !busy);
    EnableWindow(state->button_show_config, !busy);
    EnableWindow(state->button_open_downloads, TRUE);
    EnableWindow(state->button_readme, TRUE);
    EnableWindow(state->button_support, TRUE);
    set_settings_controls_enabled(state, !busy);
    update_list_view_ui(state);
    if (!busy) {
        state->download_stop_requested = 0;
    }
}

static void start_show_config(AppState* state)
{
    open_settings_window(state);
}

static void request_stop_download(AppState* state)
{
    if (!state || !state->download_cancel_event || state->download_stop_requested) {
        return;
    }

    state->download_stop_requested = 1;
    SetEvent(state->download_cancel_event);
    if (state->button_stop) {
        EnableWindow(state->button_stop, FALSE);
    }
    append_log_line(state, L"[native] Stop requested. The downloader will stop after the current item finishes.");
}

static void process_single_target(AppState* state, const wchar_t* raw_target, int* total_count, int* success_count, int* failure_count)
{
    wchar_t target[2048];
    wchar_t result_message[2048];

    safe_wcs_copy(target, 2048, raw_target);
    normalize_target_line(target);

    if (!target[0] || target[0] == L'#') {
        return;
    }

    ++(*total_count);
    append_log(state, L"[native] target: ");
    append_log_line(state, target);
    pump_pending_messages();

    if (pixiv_download_artwork(&state->paths, &state->config, target, result_message, 2048)) {
        ++(*success_count);
        mark_target_result_in_edit(state, target, 1);
        append_log(state, L"[ok] ");
        append_log_line(state, result_message);
    } else {
        ++(*failure_count);
        mark_target_result_in_edit(state, target, 0);
        append_log(state, L"[error] ");
        append_log_line(state, result_message);
        append_verbose_diagnostic(state);
    }

    set_download_progress(state, *total_count, state->planned_download_total);
    pump_pending_messages();
}

static int count_valid_targets_in_buffer(wchar_t* buffer)
{
    wchar_t* context = NULL;
    wchar_t* line = NULL;
    int count = 0;

    if (!buffer) {
        return 0;
    }

    line = wcstok_s(buffer, L"\r\n", &context);
    while (line) {
        normalize_target_line(line);
        if (line[0] && line[0] != L'#') {
            ++count;
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }

    return count;
}

static wchar_t* load_input_file_target_buffer(AppState* state, wchar_t* resolved_input_file, int* file_error)
{
    wchar_t input_file[PATH_BUFFER_COUNT];
    char* file_text_utf8 = NULL;
    wchar_t* file_text_wide = NULL;
    wchar_t* json_targets_wide = NULL;

    if (file_error) {
        *file_error = 0;
    }
    if (resolved_input_file) {
        resolved_input_file[0] = L'\0';
    }

    if (!state) {
        return NULL;
    }

    GetWindowTextW(state->edit_input_file, input_file, PATH_BUFFER_COUNT);
    trim_wide_text(input_file);
    if (!input_file[0]) {
        return NULL;
    }

    resolve_against_app_root(&state->paths, input_file, resolved_input_file, PATH_BUFFER_COUNT);
    file_text_utf8 = read_text_file_utf8(resolved_input_file);
    if (!file_text_utf8) {
        if (file_error) {
            *file_error = 1;
        }
        return NULL;
    }

    json_targets_wide = parse_json_string_array_to_wide_lines(file_text_utf8, "targets");
    if (json_targets_wide) {
        free(file_text_utf8);
        return json_targets_wide;
    }

    file_text_wide = utf8_to_wide_alloc(file_text_utf8);
    free(file_text_utf8);
    if (!file_text_wide) {
        if (file_error) {
            *file_error = 1;
        }
        return NULL;
    }

    return file_text_wide;
}

static int count_targets_from_input_file(AppState* state)
{
    wchar_t resolved_input_file[PATH_BUFFER_COUNT];
    wchar_t* buffer = NULL;
    int file_error = 0;
    int count = 0;

    buffer = load_input_file_target_buffer(state, resolved_input_file, &file_error);
    if (!buffer) {
        return 0;
    }

    count = count_valid_targets_in_buffer(buffer);
    free(buffer);
    return count;
}

static void process_target_buffer(AppState* state, wchar_t* buffer, int* total_count, int* success_count, int* failure_count)
{
    wchar_t* context = NULL;
    wchar_t* line = NULL;

    if (!buffer) {
        return;
    }

    line = wcstok_s(buffer, L"\r\n", &context);
    while (line) {
        process_single_target(state, line, total_count, success_count, failure_count);
        line = wcstok_s(NULL, L"\r\n", &context);
    }
}

static void process_input_file_targets(AppState* state, int* total_count, int* success_count, int* failure_count)
{
    wchar_t resolved_input_file[PATH_BUFFER_COUNT];
    wchar_t* buffer = NULL;
    int file_error = 0;

    buffer = load_input_file_target_buffer(state, resolved_input_file, &file_error);
    if (!buffer) {
        if (!file_error) {
            return;
        }
        ++(*failure_count);
        append_log(state, L"[error] Could not open input file: ");
        append_log_line(state, resolved_input_file);
        return;
    }

    append_log(state, L"[native] input file: ");
    append_log_line(state, resolved_input_file);
    process_target_buffer(state, buffer, total_count, success_count, failure_count);
    free(buffer);
}

static void start_download(AppState* state)
{
    int target_length = 0;
    wchar_t* targets_text = NULL;
    DownloadWorkerContext* context = NULL;
    HANDLE worker_thread = NULL;

    if (state->current_list_view != LIST_VIEW_TARGETS) {
        set_current_list_view(state, LIST_VIEW_TARGETS);
    }
    add_pending_target_from_input(state, 1, 1, 0);
    load_targets_list_to_ui(state);
    if (!has_download_targets(state)) {
        show_message(state->window, state_text(state, L"dialog_no_targets_title"), state_text(state, L"dialog_no_targets_body"), MB_ICONWARNING);
        return;
    }

    save_config_from_ui(state, 0);
    if (!state->config.download_illustrations
        && !state->config.download_manga
        && !state->config.download_novels) {
        show_message(
            state->window,
            state_text(state, L"dialog_no_download_types_title"),
            state_text(state, L"dialog_no_download_types_body"),
            MB_ICONWARNING
        );
        return;
    }
    pixiv_set_detailed_logging(Button_GetCheck(state->check_verbose) == BST_CHECKED);
    append_log_line(state, L"");
    append_log_line(state, L"[native] Starting Pixiv single-artwork download batch.");
    append_log_line(state, L"[native] Current native scope: single-page, multi-page, ugoira, and user/category Pixiv targets.");
    {
        wchar_t parallel_message[128];
        swprintf_s(
            parallel_message,
            _countof(parallel_message),
            L"[native] Parallel download workers: %d (allowed range: %d-%d).",
            state->config.parallel_downloads,
            MIN_PARALLEL_DOWNLOADS,
            MAX_PARALLEL_DOWNLOADS
        );
        append_log_line(state, parallel_message);
    }

    if (Button_GetCheck(state->check_dry_run) == BST_CHECKED) {
        append_log_line(state, L"[native] Dry-run is not implemented in C mode yet, so this run will download files normally.");
    }
    if (Button_GetCheck(state->check_verbose) == BST_CHECKED) {
        append_log_line(state, L"[native] Detailed diagnostics are enabled.");
    }
    if (Button_GetCheck(state->check_print_command) == BST_CHECKED) {
        append_log_line(state, L"[native] Print-command output is not used by the C downloader.");
    }

    target_length = GetWindowTextLengthW(state->edit_targets);
    if (target_length <= 0) {
        append_log_line(state, L"[native] No valid Pixiv targets were found.");
        return;
    }

    targets_text = (wchar_t*)calloc((size_t)target_length + 1, sizeof(wchar_t));
    if (!targets_text) {
        append_log_line(state, L"[error] Could not allocate target buffer.");
        return;
    }
    GetWindowTextW(state->edit_targets, targets_text, target_length + 1);

    context = (DownloadWorkerContext*)calloc(1, sizeof(DownloadWorkerContext));
    if (!context) {
        free(targets_text);
        append_log_line(state, L"[error] Could not allocate download worker context.");
        return;
    }

    context->window = state->window;
    context->paths = state->paths;
    context->config = state->config;
    context->targets_text = targets_text;
    context->shared_access_token[0] = L'\0';
    context->verbose_enabled = Button_GetCheck(state->check_verbose) == BST_CHECKED;
    context->cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!context->cancel_event) {
        free_download_worker_context(context);
        append_log_line(state, L"[error] Could not create the download stop event.");
        return;
    }

    clear_target_results_text(state);
    state->planned_download_total = 1;
    state->download_stop_requested = 0;
    if (state->download_cancel_event) {
        CloseHandle(state->download_cancel_event);
    }
    state->download_cancel_event = context->cancel_event;
    set_download_progress(state, 0, 1);
    set_busy_state(state, 1);

    worker_thread = CreateThread(NULL, 0, download_worker_thread, context, 0, NULL);
    if (!worker_thread) {
        CloseHandle(context->cancel_event);
        state->download_cancel_event = NULL;
        state->planned_download_total = 0;
        set_busy_state(state, 0);
        free_download_worker_context(context);
        append_log_line(state, L"[error] Could not start the download worker thread.");
        return;
    }

    CloseHandle(worker_thread);
}

static void open_auth_window(AppState* state)
{
    wchar_t clipboard_text[4096];
    wchar_t login_url[2048];
    wchar_t code_verifier[256];
    wchar_t error_message[512];

    if (read_clipboard_text(state->window, clipboard_text, 4096)
        && import_refresh_token_from_text(state, clipboard_text, 1, 1)) {
        show_message(
            state->window,
            APP_TITLE,
            state_text(state, L"dialog_imported_refresh_token"),
            MB_ICONINFORMATION
        );
        SetFocus(state->edit_token);
        return;
    }

    if (read_clipboard_text(state->window, clipboard_text, 4096)
        && import_authorization_code_from_text(state, clipboard_text, 1, 1)) {
        show_message(
            state->window,
            APP_TITLE,
            state_text(state, L"dialog_imported_auth_code"),
            MB_ICONINFORMATION
        );
        SetFocus(state->edit_token);
        return;
    }

    if (!pixiv_build_pkce_login_url(code_verifier, 256, login_url, 2048, error_message, 512)) {
        show_message(state->window, APP_TITLE, error_message, MB_ICONERROR);
        return;
    }

    safe_wcs_copy(state->auth_code_verifier, 256, code_verifier);
    state->auth_code_waiting = 1;

    ShellExecuteW(state->window, L"open", login_url, NULL, NULL, SW_SHOWNORMAL);
    append_log_line(state, state_text(state, L"log_opened_pkce_login"));
    append_log_line(state, state_text(state, L"log_copy_callback_code"));
    show_message(
        state->window,
        APP_TITLE,
        state_text(state, L"dialog_auth_instructions"),
        MB_ICONINFORMATION
    );
}

static void open_downloads_folder(AppState* state)
{
    wchar_t path[PATH_BUFFER_COUNT];
    if (settings_controls_available(state)) {
        GetWindowTextW(state->edit_download_dir, path, PATH_BUFFER_COUNT);
    } else {
        safe_wcs_copy(path, PATH_BUFFER_COUNT, state->config.download_dir);
    }
    resolve_against_app_root(&state->paths, path, path, PATH_BUFFER_COUNT);
    ShellExecuteW(state->window, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

static void open_ffmpeg_download_page(AppState* state)
{
    ShellExecuteW(state->window, L"open", L"https://www.gyan.dev/ffmpeg/builds/", NULL, NULL, SW_SHOWNORMAL);
    append_log_line(state, state_text(state, L"log_opened_ffmpeg_download"));
}

static void run_ffmpeg_winget_install(AppState* state)
{
    if (SearchPathW(NULL, L"winget.exe", NULL, 0, NULL, NULL) == 0) {
        show_message(
            state->window,
            APP_TITLE,
            state_text(state, L"dialog_winget_not_found_body"),
            MB_ICONWARNING
        );
        append_log_line(state, state_text(state, L"log_winget_not_found"));
        return;
    }

    ShellExecuteW(
        state->window,
        L"open",
        L"cmd.exe",
        L"/K winget install --id Gyan.FFmpeg.Essentials --source winget --accept-source-agreements --accept-package-agreements",
        NULL,
        SW_SHOWNORMAL
    );
    append_log_line(state, state_text(state, L"log_opened_ffmpeg_winget_install"));
}

static void open_readme(AppState* state)
{
    ShellExecuteW(state->window, L"open", state->paths.readme_path, NULL, NULL, SW_SHOWNORMAL);
}

static void open_support_page(AppState* state)
{
    ShellExecuteW(state->window, L"open", L"https://buymeacoffee.com/zernia", NULL, NULL, SW_SHOWNORMAL);
}

static void create_settings_ui(AppState* state)
{
    HWND parent = state->settings_window;
    int lx = 24;
    int ex = 150;

    if (!state || !parent) {
        return;
    }

    create_section_label_on(state, parent, state_text(state, L"group_settings"), 28, 18, 220);
    create_label_on(state, parent, state_text(state, L"label_token"), lx, 58, 120);
    state->edit_token = create_edit_on(state, parent, L"", ex, 54, 462, 28, IDC_EDIT_TOKEN, 0);
    SendMessageW(state->edit_token, EM_SETCUEBANNER, 0, (LPARAM)state_text(state, L"placeholder_refresh_token"));
    create_label_on(state, parent, state_text(state, L"label_ffmpeg"), lx, 94, 120);
    state->edit_ffmpeg = create_edit_on(state, parent, L"", ex, 90, 190, 28, IDC_EDIT_FFMPEG, 0);
    state->button_download_ffmpeg = create_button_on(state, parent, state_text(state, L"button_download_ffmpeg"), ex + 200, 90, 110, 28, IDC_BUTTON_DOWNLOAD_FFMPEG);
    state->button_install_ffmpeg = create_button_on(state, parent, state_text(state, L"button_install_ffmpeg"), ex + 320, 90, 142, 28, IDC_BUTTON_INSTALL_FFMPEG);
    create_label_on(state, parent, state_text(state, L"label_ffmpeg_detected"), lx, 130, 120);
    state->edit_ffmpeg_status = create_edit_on(state, parent, L"", ex, 126, 462, 28, IDC_EDIT_FFMPEG_STATUS, ES_READONLY);
    create_label_on(state, parent, state_text(state, L"label_ugoira_format"), lx, 166, 120);
    state->combo_ugoira = create_control_on(state, parent, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, ex, 160, 180, 300, IDC_COMBO_UGOIRA);
    populate_ugoira_combo(state->combo_ugoira);
    create_label_on(state, parent, state_text(state, L"label_sleep_request"), 350, 166, 100);
    state->edit_sleep = create_edit_on(state, parent, L"", 454, 160, 158, 28, IDC_EDIT_SLEEP, 0);
    create_label_on(state, parent, state_text(state, L"label_parallel_downloads"), lx, 202, 120);
    state->edit_parallel = create_edit_on(state, parent, L"3", ex, 198, 80, 28, IDC_EDIT_PARALLEL, 0);
    create_label_on(state, parent, state_text(state, L"label_profile_page_size"), 260, 202, 90);
    state->edit_profile_page_size = create_edit_on(state, parent, L"32", 354, 198, 70, 28, IDC_EDIT_PROFILE_PAGE_SIZE, 0);
    create_label_on(state, parent, state_text(state, L"label_language"), lx, 240, 120);
    state->combo_language = create_control_on(state, parent, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, ex, 234, 190, 300, IDC_COMBO_LANGUAGE);
    populate_language_combo(state);
    state->check_metadata = create_checkbox_on(state, parent, state_text(state, L"checkbox_write_metadata"), 350, 234, 262, 28, IDC_CHECK_METADATA);

    create_section_label_on(state, parent, state_text(state, L"group_naming_rules"), 28, 314, 220);
    create_label_on(state, parent, state_text(state, L"label_artist_folder"), lx, 354, 120);
    state->edit_artist_folder = create_edit_on(state, parent, L"", ex, 350, 462, 28, IDC_EDIT_ARTIST_FOLDER, 0);
    create_label_on(state, parent, state_text(state, L"label_multi_image_folder"), lx, 392, 120);
    state->edit_multi_folder = create_edit_on(state, parent, L"", ex, 388, 462, 28, IDC_EDIT_MULTI_FOLDER, 0);
    create_label_on(state, parent, state_text(state, L"label_single_image_filename"), lx, 430, 120);
    state->edit_single_file = create_edit_on(state, parent, L"", ex, 426, 462, 28, IDC_EDIT_SINGLE_FILE, 0);
    create_label_on(state, parent, state_text(state, L"label_multi_image_filename"), lx, 464, 120);
    state->edit_multi_file = create_edit_on(state, parent, L"", ex, 460, 462, 28, IDC_EDIT_MULTI_FILE, 0);

    create_section_label_on(state, parent, state_text(state, L"group_current_paths"), 28, 518, 220);
    create_label_on(state, parent, state_text(state, L"label_download_dir"), lx, 556, 120);
    state->edit_download_dir = create_edit_on(state, parent, L"", ex, 552, 462, 28, IDC_EDIT_DOWNLOAD_DIR, 0);
    create_label_on(state, parent, state_text(state, L"label_archive_file"), lx, 594, 120);
    state->edit_archive_file = create_edit_on(state, parent, L"", ex, 590, 462, 28, IDC_EDIT_ARCHIVE_FILE, 0);
    state->check_auto_update = create_checkbox_on(state, parent, state_text(state, L"checkbox_auto_update"), ex, 626, 260, 28, IDC_CHECK_AUTO_UPDATE);
    create_label_on(state, parent, state_text(state, L"label_update_manifest"), lx, 666, 120);
    state->edit_github_repository = create_edit_on(state, parent, L"", ex, 660, 462, 28, IDC_EDIT_UPDATE_MANIFEST, 0);
    SendMessageW(state->edit_github_repository, EM_SETCUEBANNER, 0, (LPARAM)state_text(state, L"placeholder_update_manifest"));
    state->button_save = create_button_on(state, parent, state_text(state, L"button_save_config"), 150, 704, 100, 34, IDC_BUTTON_SAVE);
    state->button_reload = create_button_on(state, parent, state_text(state, L"button_reload_config"), 260, 704, 105, 34, IDC_BUTTON_RELOAD);
    state->button_auth = create_button_on(state, parent, state_text(state, L"button_open_auth"), 375, 704, 120, 34, IDC_BUTTON_AUTH);
    state->button_check_update = create_button_on(state, parent, state_text(state, L"button_check_update"), 505, 704, 110, 34, IDC_BUTTON_CHECK_UPDATE);
}

static void open_settings_window(AppState* state)
{
    RECT rect = { 0, 0, SETTINGS_WINDOW_WIDTH, SETTINGS_WINDOW_HEIGHT };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    if (!state) {
        return;
    }

    if (state->settings_window && IsWindow(state->settings_window)) {
        ShowWindow(state->settings_window, SW_SHOWNORMAL);
        SetForegroundWindow(state->settings_window);
        return;
    }

    AdjustWindowRectEx(&rect, style, FALSE, 0);
    if (state->window && IsWindow(state->window)) {
        RECT parent_rect;
        if (GetWindowRect(state->window, &parent_rect)) {
            x = parent_rect.left + (((parent_rect.right - parent_rect.left) - (rect.right - rect.left)) / 2);
            y = parent_rect.top + (((parent_rect.bottom - parent_rect.top) - (rect.bottom - rect.top)) / 2);
            if (x < 0) {
                x = 0;
            }
            if (y < 0) {
                y = 0;
            }
        }
    }
    state->settings_window = CreateWindowExW(
        0,
        SETTINGS_WINDOW_CLASS_NAME,
        state_text(state, L"group_settings"),
        style,
        x,
        y,
        rect.right - rect.left,
        rect.bottom - rect.top,
        state->window,
        NULL,
        state->instance,
        state
    );

    if (state->settings_window) {
        SetWindowTextW(state->settings_window, state_text(state, L"group_settings"));
        ShowWindow(state->settings_window, SW_SHOWNORMAL);
        UpdateWindow(state->settings_window);
    }
}

static void create_ui(AppState* state)
{
    int rx = MAIN_CONTENT_LEFT;
    initialize_theme_resources(state);
    clear_settings_control_handles(state);

    state->slider_opacity = create_control(
        state,
        TRACKBAR_CLASSW,
        L"",
        TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
        0,
        1112,
        44,
        144,
        24,
        IDC_SLIDER_OPACITY
    );
    if (state->slider_opacity) {
        SendMessageW(state->slider_opacity, TBM_SETRANGE, TRUE, MAKELONG(35, 100));
        SendMessageW(state->slider_opacity, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(state->slider_opacity, TBM_SETPOS, TRUE, 100);
    }

    create_section_label(state, state_text(state, L"group_download"), 28, 118, 220);
    state->button_view_targets = create_button(state, state_text(state, L"button_view_targets"), rx, 146, 116, 32, IDC_BUTTON_VIEW_TARGETS);
    state->button_view_archive = create_button(state, state_text(state, L"button_view_archive"), rx + 126, 146, 156, 32, IDC_BUTTON_VIEW_ARCHIVE);
    state->label_counts_summary = create_control(state, L"STATIC", L"", SS_RIGHT, 0, rx + 974, 152, 258, 22, 0);
    state->label_targets = create_control(state, L"STATIC", state_text(state, L"label_targets_select_view"), SS_LEFT, 0, rx, 186, MAIN_CONTENT_WIDTH, 28, 0);
    apply_font(state->label_targets, state->font);
    state->edit_targets = create_edit_ex(state, L"", rx, 214, MAIN_CONTENT_WIDTH, 242, IDC_EDIT_TARGETS, ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, WS_EX_CLIENTEDGE);
    SendMessageW(state->edit_targets, EM_SETLIMITTEXT, LARGE_EDIT_TEXT_LIMIT, 0);
    create_label(state, state_text(state, L"label_target_input"), rx, 466, 180);
    state->edit_input_file = create_edit_ex(state, L"", rx, 500, 760, 30, IDC_EDIT_INPUT_FILE, 0, WS_EX_CLIENTEDGE);
    state->button_add_target = create_button(state, state_text(state, L"button_add"), rx + 776, 498, 110, 34, IDC_BUTTON_ADD_TARGET);
    state->button_delete_target = create_button(state, state_text(state, L"button_delete"), rx + 894, 498, 110, 34, IDC_BUTTON_DELETE_TARGET);
    state->button_delete_all = create_button(state, state_text(state, L"button_delete_all"), rx + 1012, 498, 112, 34, IDC_BUTTON_DELETE_ALL);
    state->progress_download = create_control(state, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, rx, 538, MAIN_CONTENT_WIDTH, 18, IDC_PROGRESS_DOWNLOAD);
    SendMessageW(state->progress_download, PBM_SETRANGE32, 0, 1);
    SendMessageW(state->progress_download, PBM_SETPOS, 0, 0);
    update_progress_theme(state);
    state->check_dry_run = create_checkbox(state, state_text(state, L"checkbox_dry_run"), rx, 560, 116, 28, IDC_CHECK_DRY_RUN);
    state->check_verbose = create_checkbox(state, state_text(state, L"checkbox_verbose"), rx + 118, 560, 128, 28, IDC_CHECK_VERBOSE);
    state->check_print_command = create_checkbox(state, state_text(state, L"checkbox_print_command"), rx + 246, 560, 136, 28, IDC_CHECK_PRINT_COMMAND);
    state->button_duplicate_download = create_button(state, state_text(state, L"button_duplicate_download_off"), rx + 392, 558, 118, 32, IDC_BUTTON_DUPLICATE_DOWNLOAD);
    state->button_auto_clipboard = create_button(state, state_text(state, L"button_auto_clipboard_off"), rx + 520, 558, 156, 32, IDC_BUTTON_AUTO_CLIPBOARD);
    state->check_illustrations = create_checkbox(state, state_text(state, L"checkbox_download_illustrations"), rx + 692, 560, 128, 28, IDC_CHECK_ILLUSTRATIONS);
    state->check_manga = create_checkbox(state, state_text(state, L"checkbox_download_manga"), rx + 824, 560, 104, 28, IDC_CHECK_MANGA);
    state->check_novels = create_checkbox(state, state_text(state, L"checkbox_download_novels"), rx + 932, 560, 104, 28, IDC_CHECK_NOVELS);
    state->button_start = create_button(state, state_text(state, L"button_start_download"), rx, 598, 118, 36, IDC_BUTTON_START);
    state->button_stop = create_button(state, state_text(state, L"button_stop_download"), rx + 128, 598, 118, 36, IDC_BUTTON_STOP);
    state->button_show_config = create_button(state, state_text(state, L"button_show_config"), rx + 258, 598, 94, 36, IDC_BUTTON_SHOW_CONFIG);
    state->button_open_downloads = create_button(state, state_text(state, L"button_open_downloads"), rx + 362, 598, 118, 36, IDC_BUTTON_OPEN_DOWNLOADS);
    state->button_readme = create_button(state, state_text(state, L"button_readme"), rx + 490, 598, 84, 36, IDC_BUTTON_README);
    state->button_support = create_button(state, state_text(state, L"button_support"), rx + 584, 598, 128, 36, IDC_BUTTON_SUPPORT);
    create_control(state, L"STATIC", state_text(state, L"label_auto_clipboard_hint"), SS_LEFT, 0, rx, 636, MAIN_CONTENT_WIDTH, 20, 0);

    create_section_label(state, state_text(state, L"group_log"), 28, 686, 220);
    state->edit_log = create_edit(state, L"", MAIN_CONTENT_LEFT, 720, MAIN_CONTENT_WIDTH, 170, IDC_EDIT_LOG, ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL);
    SendMessageW(state->edit_log, EM_SETLIMITTEXT, LARGE_EDIT_TEXT_LIMIT, 0);
    state->button_clear_log = create_button(state, state_text(state, L"button_clear_log"), MAIN_CONTENT_RIGHT - 158, 898, 158, 36, IDC_BUTTON_CLEAR_LOG);
}

static LRESULT handle_command(AppState* state, WPARAM w_param, LPARAM l_param)
{
    UNREFERENCED_PARAMETER(l_param);

    switch (LOWORD(w_param)) {
    case IDC_BUTTON_SAVE:
        save_config_from_ui(state, 1);
        return 1;
    case IDC_BUTTON_RELOAD:
        reload_config_to_ui(state);
        return 1;
    case IDC_BUTTON_AUTH:
        open_auth_window(state);
        return 1;
    case IDC_BUTTON_AUTO_CLIPBOARD:
        toggle_auto_clipboard(state);
        return 1;
    case IDC_BUTTON_DUPLICATE_DOWNLOAD:
        toggle_duplicate_download(state);
        return 1;
    case IDC_CHECK_ILLUSTRATIONS:
    case IDC_CHECK_MANGA:
    case IDC_CHECK_NOVELS:
    case IDC_CHECK_AUTO_UPDATE:
        save_config_from_ui(state, 0);
        return 1;
    case IDC_BUTTON_CHECK_UPDATE:
        start_update_check(state, 1);
        return 1;
    case IDC_BUTTON_VIEW_TARGETS:
        set_current_list_view(state, LIST_VIEW_TARGETS);
        return 1;
    case IDC_BUTTON_VIEW_ARCHIVE:
        set_current_list_view(state, LIST_VIEW_ARCHIVE);
        return 1;
    case IDC_BUTTON_ADD_TARGET:
        add_pending_target_from_input(state, 1, 1, 1);
        return 1;
    case IDC_BUTTON_DELETE_TARGET:
        delete_selected_target_from_edit(state);
        return 1;
    case IDC_BUTTON_DELETE_ALL:
        clear_current_list_view(state);
        return 1;
    case IDC_BUTTON_START:
        start_download(state);
        return 1;
    case IDC_BUTTON_STOP:
        request_stop_download(state);
        return 1;
    case IDC_BUTTON_SHOW_CONFIG:
        start_show_config(state);
        return 1;
    case IDC_BUTTON_OPEN_DOWNLOADS:
        open_downloads_folder(state);
        return 1;
    case IDC_BUTTON_DOWNLOAD_FFMPEG:
        open_ffmpeg_download_page(state);
        return 1;
    case IDC_BUTTON_INSTALL_FFMPEG:
        run_ffmpeg_winget_install(state);
        return 1;
    case IDC_BUTTON_README:
        open_readme(state);
        return 1;
    case IDC_BUTTON_SUPPORT:
        open_support_page(state);
        return 1;
    case IDC_BUTTON_CLEAR_LOG:
        SetWindowTextW(state->edit_log, L"");
        return 1;
    case IDC_EDIT_FFMPEG:
        if (HIWORD(w_param) == EN_CHANGE) {
            update_ffmpeg_status(state);
        }
        return 1;
    default:
        return 0;
    }
}

static LRESULT CALLBACK settings_window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
    AppState* state = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create_struct = (CREATESTRUCTW*)l_param;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create_struct->lpCreateParams);
        return TRUE;
    }
    case WM_CREATE:
        state = (AppState*)((CREATESTRUCTW*)l_param)->lpCreateParams;
        state->settings_window = hwnd;
        create_settings_ui(state);
        load_controls_from_config(state);
        set_busy_state(state, state->planned_download_total > 0);
        if (state->update_check_in_progress || state->update_download_in_progress) {
            EnableWindow(state->button_check_update, FALSE);
        }
        return 0;
    case WM_COMMAND:
        if (state && handle_command(state, w_param, l_param)) {
            return 0;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        paint_settings_window_background(state, dc, hwnd);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DRAWITEM:
        return draw_owner_button(state, (const DRAWITEMSTRUCT*)l_param);
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w_param;
        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_INPUT_BG);
        return (LRESULT)state->input_brush;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w_param;
        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_INPUT_BG);
        return (LRESULT)state->input_brush;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w_param;
        HWND control = (HWND)l_param;
        HBRUSH brush = NULL;
        COLORREF bg_color = COLOR_PANEL_BG;

        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

        brush = pick_background_brush_for_control(state, control, &bg_color);
        if (is_readonly_display_control(state, control)) {
            SetTextColor(dc, COLOR_TEXT);
            SetBkColor(dc, bg_color);
            return (LRESULT)brush;
        }

        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, bg_color);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)brush;
    }
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)w_param;
        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_PANEL_BG);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)state->panel_brush;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state) {
            clear_settings_control_handles(state);
            state->settings_window = NULL;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
    AppState* state = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create_struct = (CREATESTRUCTW*)l_param;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create_struct->lpCreateParams);
        return TRUE;
    }
    case WM_CREATE:
        state = (AppState*)((CREATESTRUCTW*)l_param)->lpCreateParams;
        state->window = hwnd;
        state->window_opacity_percent = 100;
        create_ui(state);
        apply_window_opacity(state, state->window_opacity_percent);
        load_controls_from_config(state);
        state->current_list_view = LIST_VIEW_NONE;
        refresh_current_list_view(state);
        state->clipboard_listener_registered = AddClipboardFormatListener(hwnd) ? 1 : 0;
        sync_clipboard_snapshot(state);
        set_busy_state(state, 0);
        if (state->config.auto_update && state->config.github_repository[0]) {
            start_update_check(state, 0);
        }
        return 0;
    case WM_APP_UPDATE_CHECKED:
        if (state) {
            UpdateCheckMessage* update = (UpdateCheckMessage*)l_param;
            state->update_check_in_progress = 0;
            if (state->button_check_update) {
                EnableWindow(state->button_check_update, TRUE);
            }
            if (update) {
                if (!update->succeeded) {
                    append_log(state, L"[error] ");
                    append_log_line(
                        state,
                        update->error_message[0]
                            ? update->error_message
                            : state_text(state, L"dialog_update_check_failed")
                    );
                    if (update->manual) {
                        wchar_t body[1024];
                        safe_wcs_copy(body, _countof(body), state_text(state, L"dialog_update_check_failed"));
                        if (update->error_message[0]) {
                            wcscat_s(body, _countof(body), L"\n\n");
                            wcscat_s(body, _countof(body), update->error_message);
                        }
                        show_message(
                            state->settings_window ? state->settings_window : state->window,
                            state_text(state, L"dialog_update_title"),
                            body,
                            MB_ICONERROR
                        );
                    }
                } else if (!update->update_available) {
                    wchar_t body[512];
                    format_state_text_one(
                        state,
                        L"dialog_update_current",
                        L"{version}",
                        YN_APP_VERSION,
                        body,
                        _countof(body)
                    );
                    append_log_line(state, body);
                    if (update->manual) {
                        show_message(
                            state->settings_window ? state->settings_window : state->window,
                            state_text(state, L"dialog_update_title"),
                            body,
                            MB_ICONINFORMATION
                        );
                    }
                } else {
                    wchar_t body[1024];
                    format_state_text_two(
                        state,
                        L"dialog_update_available",
                        L"{new_version}",
                        update->manifest.version,
                        L"{current_version}",
                        YN_APP_VERSION,
                        body,
                        _countof(body)
                    );
                    if (MessageBoxW(
                            state->window,
                            body,
                            state_text(state, L"dialog_update_title"),
                            MB_ICONINFORMATION | MB_YESNO
                        ) == IDYES) {
                        start_update_download(state, &update->manifest);
                    }
                }
                free(update);
            }
        }
        return 0;
    case WM_APP_UPDATE_DOWNLOADED:
        if (state) {
            UpdateDownloadMessage* update = (UpdateDownloadMessage*)l_param;
            state->update_download_in_progress = 0;
            if (update) {
                if (update->succeeded) {
                    wchar_t executable_path[PATH_BUFFER_COUNT];
                    wchar_t launch_error[512];
                    executable_path[0] = L'\0';
                    launch_error[0] = L'\0';
                    if (GetModuleFileNameW(NULL, executable_path, _countof(executable_path))
                        && updater_launch_apply(
                            update->staged_path,
                            GetCurrentProcessId(),
                            executable_path,
                            launch_error,
                            _countof(launch_error)
                        )) {
                        append_log_line(state, state_text(state, L"log_update_installing"));
                        free(update);
                        DestroyWindow(state->window);
                        return 0;
                    }
                    DeleteFileW(update->staged_path);
                    safe_wcs_copy(
                        update->error_message,
                        _countof(update->error_message),
                        launch_error[0] ? launch_error : state_text(state, L"dialog_update_download_failed")
                    );
                }
                append_log(state, L"[error] ");
                append_log_line(
                    state,
                    update->error_message[0]
                        ? update->error_message
                        : state_text(state, L"dialog_update_download_failed")
                );
                {
                    wchar_t body[1024];
                    safe_wcs_copy(body, _countof(body), state_text(state, L"dialog_update_download_failed"));
                    if (update->error_message[0]) {
                        wcscat_s(body, _countof(body), L"\n\n");
                        wcscat_s(body, _countof(body), update->error_message);
                    }
                    show_message(
                        state->window,
                        state_text(state, L"dialog_update_title"),
                        body,
                        MB_ICONERROR
                    );
                }
                free(update);
            }
        }
        return 0;
    case WM_APP_DOWNLOAD_PREPARED:
        if (state) {
            DownloadPreparedMessage* prepared = (DownloadPreparedMessage*)l_param;
            if (prepared) {
                if (prepared->failed) {
                    append_log(state, L"[error] ");
                    append_log_line(state, prepared->error_message[0] ? prepared->error_message : L"Could not prepare Pixiv batch targets.");
                    if (prepared->detail_message[0]) {
                        append_log(state, L"[detail] ");
                        append_log_line(state, prepared->detail_message);
                    }
                    if (state->download_cancel_event) {
                        CloseHandle(state->download_cancel_event);
                        state->download_cancel_event = NULL;
                    }
                    state->planned_download_total = 0;
                    set_busy_state(state, 0);
                } else {
                    wchar_t* clean_targets = normalized_lines_copy_alloc(prepared->expanded_targets_text ? prepared->expanded_targets_text : L"");
                    if (clean_targets) {
                        set_target_results_text(state, clean_targets);
                        free(clean_targets);
                    }
                    state->planned_download_total = prepared->prepared_count;
                    set_download_progress(state, 0, prepared->prepared_count);
                    if (state->current_list_view == LIST_VIEW_TARGETS && state->edit_targets && state->target_results_text) {
                        set_edit_text_preserve_view(state->edit_targets, state->target_results_text);
                    }
                    update_list_view_ui(state);
                }
                free(prepared->expanded_targets_text);
                free(prepared);
            }
        }
        return 0;
    case WM_APP_DOWNLOAD_RESULT:
        if (state) {
            DownloadResultMessage* message = (DownloadResultMessage*)l_param;
            if (message) {
                append_log(state, L"[native] target: ");
                append_log_line(state, message->target);
                if (message->succeeded) {
                    mark_target_result_in_edit(state, message->target, 1);
                    append_log(state, L"[ok] ");
                    append_log_line(state, message->result_message);
                } else {
                    mark_target_result_in_edit(state, message->target, 0);
                    append_log(state, L"[error] ");
                    append_log_line(state, message->result_message);
                    if (message->detail_message[0]) {
                        append_log(state, L"[detail] ");
                        append_log_line(state, message->detail_message);
                    }
                }
                update_download_counts(state);
                if (state->current_list_view == LIST_VIEW_ARCHIVE) {
                    refresh_archive_list_preserve_view(state);
                }
                if (message->total_count > (int)SendMessageW(state->progress_download, PBM_GETPOS, 0, 0)) {
                    set_download_progress(state, message->total_count, state->planned_download_total);
                }
                free(message);
            }
        }
        return 0;
    case WM_APP_DOWNLOAD_FINISHED:
        if (state) {
            DownloadFinishedMessage* message = (DownloadFinishedMessage*)l_param;
            if (message) {
                wchar_t summary[256];
                wchar_t saved_log_path[PATH_BUFFER_COUNT];

                if (message->total_count == 0) {
                    append_log_line(state, L"[native] No valid Pixiv targets were found.");
                }
                swprintf_s(
                    summary,
                    _countof(summary),
                    message->cancelled ? L"[native] Stopped. total=%d success=%d failed=%d" : L"[native] Finished. total=%d success=%d failed=%d",
                    message->total_count,
                    message->success_count,
                    message->failure_count
                );
                append_log_line(state, summary);
                if (save_log_to_file(state, saved_log_path, _countof(saved_log_path))) {
                    append_log(state, L"[native] Saved detailed log: ");
                    append_log_line(state, saved_log_path);
                } else {
                    append_log_line(state, L"[error] Could not save config/log.txt.");
                }
                set_download_progress(state, message->total_count, state->planned_download_total);
                update_download_counts(state);
                if (state->download_cancel_event) {
                    CloseHandle(state->download_cancel_event);
                    state->download_cancel_event = NULL;
                }
                state->planned_download_total = 0;
                set_busy_state(state, 0);
                free(message);
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        paint_window_background(state, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DRAWITEM:
        return draw_owner_button(state, (const DRAWITEMSTRUCT*)l_param);
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w_param;
        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_INPUT_BG);
        return (LRESULT)state->input_brush;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w_param;
        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_INPUT_BG);
        return (LRESULT)state->input_brush;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w_param;
        HWND control = (HWND)l_param;
        HBRUSH brush = NULL;
        COLORREF bg_color = COLOR_APP_BG;

        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

        brush = pick_background_brush_for_control(state, control, &bg_color);
        if (control == state->label_targets || control == state->label_counts_summary) {
            SetTextColor(dc, COLOR_MUTED_TEXT);
            SetBkColor(dc, bg_color);
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)brush;
        }
        if (is_readonly_display_control(state, control)) {
            SetTextColor(dc, COLOR_TEXT);
            SetBkColor(dc, bg_color);
            return (LRESULT)brush;
        }

        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, bg_color);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)brush;
    }
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)w_param;
        HWND control = (HWND)l_param;
        HBRUSH brush = NULL;
        COLORREF bg_color = COLOR_PANEL_BG;

        if (!state) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

        brush = pick_background_brush_for_control(state, control, &bg_color);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, bg_color);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)brush;
    }
    case WM_COMMAND:
        if (handle_command(state, w_param, l_param)) {
            return 0;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    case WM_HSCROLL:
        if (state && (HWND)l_param == state->slider_opacity) {
            int opacity = (int)SendMessageW(state->slider_opacity, TBM_GETPOS, 0, 0);
            apply_window_opacity(state, opacity);
            return 0;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    case WM_CLIPBOARDUPDATE:
        handle_clipboard_update(state);
        return 0;
    case WM_DESTROY:
        if (state && state->settings_window && IsWindow(state->settings_window)) {
            DestroyWindow(state->settings_window);
            state->settings_window = NULL;
        }
        if (state && state->download_cancel_event) {
            CloseHandle(state->download_cancel_event);
            state->download_cancel_event = NULL;
        }
        if (state && state->clipboard_listener_registered) {
            RemoveClipboardFormatListener(hwnd);
            state->clipboard_listener_registered = 0;
        }
        mciSendStringW(L"close pixiv_auto_clipboard_siren", NULL, 0, NULL);
        clear_target_results_text(state);
        destroy_theme_resources(state);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous_instance, LPWSTR command_line, int show_command)
{
    INITCOMMONCONTROLSEX common_controls;
    WNDCLASSEXW window_class;
    WNDCLASSEXW settings_class;
    HWND window = NULL;
    MSG message;
    AppState* state = NULL;
    wchar_t error_message[256];
    HICON large_icon = NULL;
    HICON small_icon = NULL;
    RECT window_rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    RECT work_area;
    DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    DWORD window_ex_style = WS_EX_LAYERED;
    int window_x = CW_USEDEFAULT;
    int window_y = CW_USEDEFAULT;
    int outer_width = 0;
    int outer_height = 0;
    int argument_count = 0;
    LPWSTR* arguments = NULL;

    UNREFERENCED_PARAMETER(previous_instance);
    UNREFERENCED_PARAMETER(command_line);

    arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments && argument_count >= 4 && wcscmp(arguments[1], L"--apply-update") == 0) {
        DWORD parent_process_id = (DWORD)wcstoul(arguments[2], NULL, 10);
        wchar_t target_path[PATH_BUFFER_COUNT];
        int applied = 0;
        safe_wcs_copy(target_path, _countof(target_path), arguments[3]);
        LocalFree(arguments);
        applied = updater_apply_staged_update(parent_process_id, target_path);
        return applied ? 0 : 1;
    }
    if (arguments) {
        LocalFree(arguments);
    }

    state = (AppState*)calloc(1, sizeof(AppState));
    if (!state) {
        return 1;
    }

    state->instance = instance;

    ZeroMemory(&common_controls, sizeof(common_controls));
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&common_controls);

    if (!discover_app_paths(&state->paths)) {
        MessageBoxW(NULL, L"Could not find the pixiv folder.", APP_TITLE, MB_ICONERROR);
        free(state);
        return 1;
    }

    config_set_defaults(&state->config);
    config_load(&state->paths, &state->config, error_message, 256);
    safe_wcs_copy(state->loaded_language, 32, state->config.language);
    locale_load_bundle(&state->paths, state->loaded_language, &state->locale);

    if (state->paths.icon_ico_path[0] && GetFileAttributesW(state->paths.icon_ico_path) != INVALID_FILE_ATTRIBUTES) {
        large_icon = (HICON)LoadImageW(
            NULL,
            state->paths.icon_ico_path,
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_LOADFROMFILE
        );
        small_icon = (HICON)LoadImageW(
            NULL,
            state->paths.icon_ico_path,
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE
        );
    }

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = WINDOW_CLASS_NAME;
    if (!large_icon) {
        large_icon = (HICON)LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR
        );
    }
    if (!small_icon) {
        small_icon = (HICON)LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR
        );
    }
    window_class.hIcon = large_icon;
    window_class.hIconSm = small_icon;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = NULL;

    RegisterClassExW(&window_class);

    ZeroMemory(&settings_class, sizeof(settings_class));
    settings_class.cbSize = sizeof(settings_class);
    settings_class.style = CS_HREDRAW | CS_VREDRAW;
    settings_class.lpfnWndProc = settings_window_proc;
    settings_class.hInstance = instance;
    settings_class.lpszClassName = SETTINGS_WINDOW_CLASS_NAME;
    settings_class.hIcon = large_icon;
    settings_class.hIconSm = small_icon;
    settings_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    settings_class.hbrBackground = NULL;
    RegisterClassExW(&settings_class);

    AdjustWindowRectEx(&window_rect, window_style, FALSE, window_ex_style);
    outer_width = window_rect.right - window_rect.left;
    outer_height = window_rect.bottom - window_rect.top;
    ZeroMemory(&work_area, sizeof(work_area));
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        int work_width = work_area.right - work_area.left;
        int work_height = work_area.bottom - work_area.top;
        window_x = work_area.left + (work_width - outer_width) / 2;
        window_y = work_area.top + (work_height - outer_height) / 2;
        if (window_x < work_area.left) {
            window_x = work_area.left;
        }
        if (window_y < work_area.top) {
            window_y = work_area.top;
        }
    }

    window = CreateWindowExW(
        window_ex_style,
        WINDOW_CLASS_NAME,
        APP_TITLE,
        window_style,
        window_x,
        window_y,
        outer_width,
        outer_height,
        NULL,
        NULL,
        instance,
        state
    );

    if (window) {
        if (large_icon) {
            SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)large_icon);
        }
        if (small_icon) {
            SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
        }
        SetWindowTextW(window, APP_TITLE);
    }

    if (!window) {
        free(state);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    free(state);
    return (int)message.wParam;
}
