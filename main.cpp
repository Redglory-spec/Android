// ============================================================================
// main.cpp
// Bot Keep Stock Otomatis v6.1 (Native Win32 C++17 Modern UI Edition)
// Fitur Utama:
// - Modern Card Dashboard UI + Dark Mode / Light Mode Toggle
// - Real-Time Header Stat Counters (Total, Sukses, Gagal, Sisa)
// - Unified Smart Table (Buka Excel / Drag & Drop .xlsx / Copy-Paste Langsung)
// - Responsive Auto-Stretch Columns, Zebra Striping & Color-Coded Live Status
// - Deteksi Duplikat & Validasi Data + 1-Klik Hapus Duplikat
// - Multi-Browser Paralel (1-4 Worker) + Batas Retry & Tombol Ulangi Gagal
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "xlsx_reader.hpp"
#include "json_config.hpp"
#include "webdriver_client.hpp"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// Aktifkan Visual Styles Modern (ComCtl32 v6) untuk MSVC
#if defined(_MSC_VER)
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

using WebDriver::Utf8ToWide;
using WebDriver::WideToUtf8;

// ============================================================================
// KONSTANTA & ID KONTROL GUI
// ============================================================================
static const char* FILE_KONFIG_TOKO = "config_toko.json";
static const char* FILE_PREFERENSI  = "user_prefs.json";

enum ControlID {
    IDC_BTN_THEME = 1000,
    IDC_BTN_PILIH_FILE = 1001,
    IDC_BTN_PASTE_CLIP,
    IDC_BTN_CEK_VALIDASI,
    IDC_BTN_HAPUS_BARIS,
    IDC_BTN_RESET_TABEL,
    IDC_ENT_M_KEEP,
    IDC_ENT_M_SKU,
    IDC_ENT_M_QTY,
    IDC_ENT_M_GONDOLA,
    IDC_BTN_TAMBAH_M,
    IDC_TREE_DATA,
    IDC_INLINE_EDIT,

    IDC_COMBO_TOKO,
    IDC_BTN_KELOLA_TOKO,
    IDC_COMBO_JENIS,
    IDC_SPIN_EDIT_MULAI,
    IDC_SPIN_UPDOWN_MULAI,
    IDC_SPIN_EDIT_RETRY,
    IDC_SPIN_UPDOWN_RETRY,
    IDC_ENTRY_NAMA,
    IDC_COMBO_POSISI,
    IDC_COMBO_WORKER,
    IDC_CHK_HEADLESS,

    IDC_PROGRESS_BAR,
    IDC_BTN_START,
    IDC_BTN_PAUSE,
    IDC_BTN_STOP,
    IDC_BTN_RETRY_FAILED,
    IDC_BTN_CLEAR_LOG,
    IDC_RICH_LOG,

    // Kontrol Dialog Kelola Toko
    IDC_TOKO_LISTBOX = 2001,
    IDC_TOKO_ENT_KODE,
    IDC_TOKO_ENT_IN,
    IDC_TOKO_ENT_OUT,
    IDC_TOKO_BTN_SIMPAN,
    IDC_TOKO_BTN_HAPUS
};

// Custom Window Messages untuk komunikasi Thread-Safe dari Worker Thread ke GUI
enum AppMessage {
    WM_APP_LOG = WM_APP + 1,
    WM_APP_STATUS,
    WM_APP_ETA,
    WM_APP_PROGRESS,
    WM_APP_ROW_STATUS,
    WM_APP_FINISHED
};

struct LogPayload {
    std::wstring text;
    COLORREF color;
};

struct StatusPayload {
    std::wstring text;
    COLORREF color;
};

struct RowStatusPayload {
    int row_index;
    std::wstring status_text;
    bool auto_scroll;
};

struct FinishedPayload {
    bool was_stopped;
    bool had_system_error;
    int last_success_row;
    int success_count_session;
    int total_data;
    std::vector<int> failed_rows;
    std::wstring error_msg;
};

// ============================================================================
// STATE GLOBAL APLIKASI & TEMA MODERN v6.1
// ============================================================================
static HINSTANCE g_hInst = nullptr;
static HWND g_hMainWnd = nullptr;
static HFONT g_hFontHeaderTitle = nullptr;
static HFONT g_hFontHeaderSub   = nullptr;
static HFONT g_hFontStatNum     = nullptr;
static HFONT g_hFontStatLbl     = nullptr;
static HFONT g_hFontCardTitle   = nullptr;
static HFONT g_hFontBold9       = nullptr;
static HFONT g_hFontReg9        = nullptr;
static HFONT g_hFontItalic9     = nullptr;
static HFONT g_hFontBold8       = nullptr;
static HFONT g_hFontReg8        = nullptr;
static HFONT g_hFontItalic8     = nullptr;
static HFONT g_hFontTips7       = nullptr;
static HFONT g_hFontLog         = nullptr;

// Tema Dark / Light Mode & Brush Cache
static bool g_isDarkMode = false;
static HBRUSH g_hBrushWinBg   = nullptr;
static HBRUSH g_hBrushCardBg  = nullptr;
static HBRUSH g_hBrushInputBg = nullptr;
static HWND   g_hHoveredBtn   = nullptr;

// Statistik Dashboard Real-Time
static int g_statTotal  = 0;
static int g_statSukses = 0;
static int g_statGagal  = 0;
static int g_statSisa   = 0;

// Koordinat Kartu Modern (Digambar via WM_PAINT)
static RECT g_rcHeader       = {0, 0, 0, 54};
static RECT g_rcCardData     = {0};
static RECT g_rcCardConfig   = {0};
static RECT g_rcCardProgress = {0};
static RECT g_rcCardLog      = {0};

// Kontrol Header
static HWND g_btnTheme = nullptr;

// Kontrol Kartu 1: Sumber Data & Tabel Terpadu
static HWND g_btnPilihFile    = nullptr;
static HWND g_btnPasteClip    = nullptr;
static HWND g_btnCekValidasi  = nullptr;
static HWND g_btnHapusBaris   = nullptr;
static HWND g_btnResetTabel   = nullptr;
static HWND g_lblFileTerpilih = nullptr;
static HWND g_lblKeep = nullptr, g_entKeep = nullptr;
static HWND g_lblSku  = nullptr, g_entSku  = nullptr;
static HWND g_lblQty  = nullptr, g_entQty  = nullptr;
static HWND g_lblGondola = nullptr, g_entGondola = nullptr;
static HWND g_btnTambahM  = nullptr;
static HWND g_lblInfoTabel = nullptr;
static HWND g_treeData    = nullptr;
static HWND g_lblTips     = nullptr;

// Inline cell edit state
static HWND g_hInlineEdit = nullptr;
static int  g_editRow = -1;
static int  g_editCol = -1;
static WNDPROC g_origInlineEditProc = nullptr;
static WNDPROC g_origGondolaEditProc = nullptr;
static WNDPROC g_origListViewProc = nullptr;

// Kontrol Kartu 2: Pengaturan
static HWND g_lblPilihToko = nullptr, g_comboToko = nullptr, g_btnKelolaToko = nullptr;
static HWND g_lblJenisForm = nullptr, g_comboJenis = nullptr;
static HWND g_lblMulaiData = nullptr, g_spinEditMulai = nullptr, g_spinUpDownMulai = nullptr;
static HWND g_lblMaxRetry  = nullptr, g_spinEditRetry = nullptr, g_spinUpDownRetry = nullptr;
static HWND g_lblNamaPic   = nullptr, g_entryNama = nullptr;
static HWND g_lblPosisi    = nullptr, g_comboPosisi = nullptr;
static HWND g_lblWorker    = nullptr, g_comboWorker = nullptr;
static HWND g_chkHeadless  = nullptr;

// Kontrol Kartu 3: Status & Progress
static HWND g_lblStatus   = nullptr;
static HWND g_lblEta      = nullptr;
static HWND g_progressBar = nullptr;

// Tombol Kontrol Utama
static HWND g_btnStart       = nullptr;
static HWND g_btnPause       = nullptr;
static HWND g_btnStop        = nullptr;
static HWND g_btnRetryFailed = nullptr;

// Kontrol Kartu 4: Live Log
static HWND g_btnClearLog = nullptr;
static HWND g_richLog     = nullptr;

// Warna dinamis untuk label
static COLORREF g_colFileLabel   = RGB(100, 116, 139);
static COLORREF g_colStatusLabel = RGB(100, 116, 139);

// Data & Konfigurasi
static JsonConfig::StoreDatabase g_databaseForm;
static std::string g_filepathExcel;
static std::vector<int> g_failedRowIndices;
static std::atomic<bool> g_isPaused{false};
static std::atomic<bool> g_isStopped{false};
static std::atomic<bool> g_isRunning{false};

// ============================================================================
// PALET WARNA TEMA (LIGHT MODE vs DARK MODE)
// ============================================================================
static COLORREF ThemeWinBg()        { return g_isDarkMode ? RGB(15, 23, 42)   : RGB(241, 245, 249); }
static COLORREF ThemeCardBg()       { return g_isDarkMode ? RGB(30, 41, 59)   : RGB(255, 255, 255); }
static COLORREF ThemeCardBorder()   { return g_isDarkMode ? RGB(51, 65, 85)   : RGB(203, 213, 225); }
static COLORREF ThemeCardTitle()    { return g_isDarkMode ? RGB(96, 165, 250) : RGB(30, 64, 175);   }
static COLORREF ThemeTextPrimary()  { return g_isDarkMode ? RGB(241, 245, 249): RGB(30, 41, 59);    }
static COLORREF ThemeTextMuted()    { return g_isDarkMode ? RGB(148, 163, 184): RGB(100, 116, 139); }
static COLORREF ThemeInputBg()      { return g_isDarkMode ? RGB(15, 23, 42)   : RGB(255, 255, 255); }
static COLORREF ThemeTableBg()      { return g_isDarkMode ? RGB(15, 23, 42)   : RGB(255, 255, 255); }
static COLORREF ThemeTableAltBg()   { return g_isDarkMode ? RGB(23, 33, 53)   : RGB(248, 250, 252); }

static void RebuildThemeBrushes() {
    if (g_hBrushWinBg)   DeleteObject(g_hBrushWinBg);
    if (g_hBrushCardBg)  DeleteObject(g_hBrushCardBg);
    if (g_hBrushInputBg) DeleteObject(g_hBrushInputBg);

    g_hBrushWinBg   = CreateSolidBrush(ThemeWinBg());
    g_hBrushCardBg  = CreateSolidBrush(ThemeCardBg());
    g_hBrushInputBg = CreateSolidBrush(ThemeInputBg());
}

static void SetWindowDarkTitleBar(HWND hWnd, bool dark) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
        if (fn) {
            BOOL useDark = dark ? TRUE : FALSE;
            fn(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark));
            fn(hWnd, 19 /* fallback Windows 10 lama */, &useDark, sizeof(useDark));
        }
        FreeLibrary(hDwm);
    }
}

// ============================================================================
// FUNGSI UTILITAS WAKTU & TEKS
// ============================================================================
static std::string FormatWaktu(double detik_raw) {
    int detik = static_cast<int>(std::max(0.0, detik_raw));
    int menit = detik / 60;
    int dtk = detik % 60;
    int jam = menit / 60;
    menit %= 60;

    std::ostringstream oss;
    if (jam > 0) {
        oss << jam << "j " << menit << "m " << dtk << "d";
    } else if (menit > 0) {
        oss << menit << "m " << dtk << "d";
    } else {
        oss << dtk << " detik";
    }
    return oss.str();
}

static std::string GetCurrentTimeStr() {
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
#if defined(_MSC_VER)
    localtime_s(&tm_now, &now);
#else
    tm_now = *std::localtime(&now);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_now);
    return buf;
}

static std::string GetWindowTextUtf8(HWND hWnd) {
    int len = GetWindowTextLengthW(hWnd);
    if (len <= 0) return "";
    std::wstring wstr(len, L'\0');
    GetWindowTextW(hWnd, &wstr[0], len + 1);
    return WideToUtf8(wstr);
}

static std::string TrimString(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ============================================================================
// STATISTIK DASHBOARD & LOGGING THREAD-SAFE
// ============================================================================
static void RecalculateDashboardStats() {
    if (!g_treeData) return;
    int total = ListView_GetItemCount(g_treeData);
    int sukses = 0;
    int gagal = 0;
    wchar_t buf[128];

    for (int i = 0; i < total; ++i) {
        buf[0] = L'\0';
        ListView_GetItemText(g_treeData, i, 5, buf, 128);
        std::wstring st(buf);
        if (st.find(L"Sukses") != std::wstring::npos) {
            sukses++;
        } else if (st.find(L"Gagal") != std::wstring::npos) {
            gagal++;
        }
    }

    g_statTotal  = total;
    g_statSukses = sukses;
    g_statGagal  = gagal;
    g_statSisa   = std::max(0, total - sukses - gagal);

    if (g_hMainWnd) {
        InvalidateRect(g_hMainWnd, &g_rcHeader, FALSE);
    }
}

static void TulisLog(const std::string& pesan, const std::string& tipe = "INFO") {
    std::string baris = "[" + GetCurrentTimeStr() + "] " + pesan + "\r\n";
    COLORREF col = RGB(0x64, 0xB5, 0xF6); // INFO (#64B5F6)
    if (tipe == "SUKSES") col = RGB(0x4A, 0xDE, 0x80); // #4ADE80
    else if (tipe == "ERROR") col = RGB(0xF8, 0x71, 0x71); // #F87171
    else if (tipe == "WARN")  col = RGB(0xFB, 0xBF, 0x24); // #FBBF24

    auto* payload = new LogPayload{Utf8ToWide(baris), col};
    PostMessageW(g_hMainWnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(payload));
}

static void SetStatusAsync(const std::string& text, COLORREF color) {
    auto* payload = new StatusPayload{Utf8ToWide(text), color};
    PostMessageW(g_hMainWnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(payload));
}

static void SetEtaAsync(const std::string& text) {
    auto* wstr = new std::wstring(Utf8ToWide(text));
    PostMessageW(g_hMainWnd, WM_APP_ETA, 0, reinterpret_cast<LPARAM>(wstr));
}

static void SetProgressAsync(int pos_x2, int max_x2) {
    PostMessageW(g_hMainWnd, WM_APP_PROGRESS, static_cast<WPARAM>(pos_x2), static_cast<LPARAM>(max_x2));
}

static void SetRowStatusAsync(int row_index, const std::string& status_utf8, bool auto_scroll = false) {
    auto* payload = new RowStatusPayload{row_index, Utf8ToWide(status_utf8), auto_scroll};
    PostMessageW(g_hMainWnd, WM_APP_ROW_STATUS, 0, reinterpret_cast<LPARAM>(payload));
}

static void UpdateRetryFailedButtonState() {
    if (!g_btnRetryFailed) return;
    int cnt = static_cast<int>(g_failedRowIndices.size());
    std::wstring label = L"↻ ULANGI GAGAL (" + std::to_wstring(cnt) + L")";
    SetWindowTextW(g_btnRetryFailed, label.c_str());
    EnableWindow(g_btnRetryFailed, (!g_isRunning.load() && cnt > 0) ? TRUE : FALSE);
    InvalidateRect(g_btnRetryFailed, nullptr, TRUE);
}

static void AppendRichLogOnMainThread(const std::wstring& text, COLORREF color) {
    if (!g_richLog) return;

    CHARRANGE cr;
    cr.cpMin = -1;
    cr.cpMax = -1;
    SendMessageW(g_richLog, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));

    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = color;
    cf.yHeight = 170; // ~8.5pt
    wcscpy_s(cf.szFaceName, L"Consolas");
    SendMessageW(g_richLog, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    SendMessageW(g_richLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g_richLog, WM_VSCROLL, SB_BOTTOM, 0);
}

// ============================================================================
// TOMBOL MODERN ROUNDED DENGAN EFEK HOVER
// ============================================================================
struct ButtonStyle {
    COLORREF bg_normal;
    COLORREF bg_disabled;
    COLORREF fg_text;
};

static std::map<int, ButtonStyle> g_buttonStyles = {
    {IDC_BTN_THEME,        {RGB(30, 41, 59),   RGB(71, 85, 105),  RGB(248, 250, 252)}},
    {IDC_BTN_PILIH_FILE,   {RGB(37, 99, 235),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_PASTE_CLIP,   {RGB(79, 70, 229),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_CEK_VALIDASI, {RGB(13, 148, 136), RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_HAPUS_BARIS,  {RGB(225, 29, 72),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_RESET_TABEL,  {RGB(100, 116, 139),RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_TAMBAH_M,     {RGB(22, 163, 74),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_KELOLA_TOKO,  {RGB(71, 85, 105),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_START,        {RGB(22, 163, 74),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_PAUSE,        {RGB(245, 158, 11), RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_STOP,         {RGB(220, 38, 38),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_RETRY_FAILED, {RGB(124, 58, 237), RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_BTN_CLEAR_LOG,    {RGB(71, 85, 105),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_TOKO_BTN_SIMPAN,  {RGB(22, 163, 74),  RGB(148, 163, 184),RGB(255, 255, 255)}},
    {IDC_TOKO_BTN_HAPUS,   {RGB(220, 38, 38),  RGB(148, 163, 184),RGB(255, 255, 255)}}
};

static std::map<HWND, WNDPROC> g_origBtnProcs;

static LRESULT CALLBACK ModernButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SETCURSOR:
            if (IsWindowEnabled(hWnd)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE:
            if (g_hHoveredBtn != hWnd) {
                g_hHoveredBtn = hWnd;
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (g_hHoveredBtn == hWnd) {
                g_hHoveredBtn = nullptr;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
    }
    return CallWindowProcW(g_origBtnProcs[hWnd], hWnd, msg, wParam, lParam);
}

static void SubclassModernButton(HWND hBtn) {
    if (!hBtn) return;
    g_origBtnProcs[hBtn] = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hBtn, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ModernButtonSubclassProc)));
}

static COLORREF AdjustColorBrightness(COLORREF col, int delta) {
    int r = std::clamp(static_cast<int>(GetRValue(col)) + delta, 0, 255);
    int g = std::clamp(static_cast<int>(GetGValue(col)) + delta, 0, 255);
    int b = std::clamp(static_cast<int>(GetBValue(col)) + delta, 0, 255);
    return RGB(r, g, b);
}

static void DrawColoredButton(LPDRAWITEMSTRUCT dis) {
    int id = static_cast<int>(dis->CtlID);
    auto it = g_buttonStyles.find(id);
    COLORREF bg = RGB(100, 116, 139);
    COLORREF fg = RGB(255, 255, 255);

    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool hovered  = (dis->hwndItem == g_hHoveredBtn) && !disabled;

    if (it != g_buttonStyles.end()) {
        bg = disabled ? (g_isDarkMode ? RGB(51, 65, 85) : RGB(203, 213, 225)) : it->second.bg_normal;
        fg = disabled ? (g_isDarkMode ? RGB(100, 116, 139) : RGB(100, 116, 139)) : it->second.fg_text;
    }

    if (pressed && !disabled) {
        bg = AdjustColorBrightness(bg, -24);
    } else if (hovered) {
        bg = AdjustColorBrightness(bg, 22);
    }

    // Isi area sudut luar tombol sesuai warna permukaan di belakangnya
    COLORREF parent_bg = ThemeCardBg();
    if (id == IDC_BTN_THEME) {
        parent_bg = g_isDarkMode ? RGB(2, 6, 23) : RGB(15, 23, 42);
    } else if (id == IDC_BTN_START || id == IDC_BTN_PAUSE || id == IDC_BTN_STOP || id == IDC_BTN_RETRY_FAILED) {
        parent_bg = ThemeWinBg();
    } else if (id == IDC_TOKO_BTN_SIMPAN || id == IDC_TOKO_BTN_HAPUS) {
        parent_bg = GetSysColor(COLOR_BTNFACE);
    }

    HBRUSH hParentBrush = CreateSolidBrush(parent_bg);
    FillRect(dis->hDC, &dis->rcItem, hParentBrush);
    DeleteObject(hParentBrush);

    // Gambar Rounded Button
    COLORREF border_col = (id == IDC_BTN_THEME)
        ? RGB(71, 85, 105)
        : AdjustColorBrightness(bg, disabled ? -10 : -28);

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border_col);
    HGDIOBJ oldBrush = SelectObject(dis->hDC, hBrush);
    HGDIOBJ oldPen   = SelectObject(dis->hDC, hPen);

    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 8, 8);

    SelectObject(dis->hDC, oldPen);
    SelectObject(dis->hDC, oldBrush);
    DeleteObject(hPen);
    DeleteObject(hBrush);

    wchar_t text[128] = {0};
    GetWindowTextW(dis->hwndItem, text, 128);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    RECT rcText = dis->rcItem;
    if (pressed && !disabled) {
        OffsetRect(&rcText, 1, 1);
    }
    DrawTextW(dis->hDC, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ============================================================================
// RENDERER DASHBOARD MODERN (HEADER BANNER, STAT PILLS & CARDS)
// ============================================================================
static void DrawModernCard(HDC hdc, const RECT& rc, const wchar_t* title) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;

    HBRUSH hCardBrush = CreateSolidBrush(ThemeCardBg());
    HPEN   hCardPen   = CreatePen(PS_SOLID, 1, ThemeCardBorder());
    HGDIOBJ oldB = SelectObject(hdc, hCardBrush);
    HGDIOBJ oldP = SelectObject(hdc, hCardPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);

    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(hCardPen);
    DeleteObject(hCardBrush);

    // Aksen bar kecil di kiri judul kartu
    RECT rcAccent = {rc.left + 12, rc.top + 10, rc.left + 16, rc.top + 24};
    HBRUSH hAccent = CreateSolidBrush(RGB(37, 99, 235));
    FillRect(hdc, &rcAccent, hAccent);
    DeleteObject(hAccent);

    // Teks judul kartu
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, ThemeCardTitle());
    HGDIOBJ oldFont = SelectObject(hdc, g_hFontCardTitle);
    RECT rcTitle = {rc.left + 22, rc.top + 8, rc.right - 100, rc.top + 26};
    DrawTextW(hdc, title, -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

static void DrawStatPill(HDC hdc, int x, int y, int w, int h,
                         const wchar_t* label, int value, COLORREF accent_col) {
    HBRUSH hPillBg = CreateSolidBrush(RGB(30, 41, 59));
    HPEN   hPillPen = CreatePen(PS_SOLID, 1, accent_col);
    HGDIOBJ oldB = SelectObject(hdc, hPillBg);
    HGDIOBJ oldP = SelectObject(hdc, hPillPen);

    RoundRect(hdc, x, y, x + w, y + h, 8, 8);

    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(hPillPen);
    DeleteObject(hPillBg);

    SetBkMode(hdc, TRANSPARENT);

    // Label kecil di atas
    HGDIOBJ oldF = SelectObject(hdc, g_hFontStatLbl);
    SetTextColor(hdc, RGB(148, 163, 184));
    RECT rcLbl = {x + 2, y + 3, x + w - 2, y + 15};
    DrawTextW(hdc, label, -1, &rcLbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Angka nilai di bawah
    SelectObject(hdc, g_hFontStatNum);
    SetTextColor(hdc, accent_col);
    std::wstring wval = std::to_wstring(value);
    RECT rcVal = {x + 2, y + 15, x + w - 2, y + h - 2};
    DrawTextW(hdc, wval.c_str(), -1, &rcVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldF);
}

static void PaintMainDashboard(HDC hdc, int client_w, int client_h) {
    // 1. Background Utama
    RECT rcClient = {0, 0, client_w, client_h};
    FillRect(hdc, &rcClient, g_hBrushWinBg);

    // 2. Top Banner Header Modern
    g_rcHeader = {0, 0, client_w, 54};
    HBRUSH hHdrBrush = CreateSolidBrush(g_isDarkMode ? RGB(2, 6, 23) : RGB(15, 23, 42));
    FillRect(hdc, &g_rcHeader, hHdrBrush);
    DeleteObject(hHdrBrush);

    // Garis aksen biru di bawah header
    RECT rcHdrLine = {0, 52, client_w, 54};
    HBRUSH hLineBrush = CreateSolidBrush(RGB(37, 99, 235));
    FillRect(hdc, &rcHdrLine, hLineBrush);
    DeleteObject(hLineBrush);

    SetBkMode(hdc, TRANSPARENT);

    // Judul & Subtitle di kiri Header
    HGDIOBJ oldF = SelectObject(hdc, g_hFontHeaderTitle);
    SetTextColor(hdc, RGB(248, 250, 252));
    RECT rcTitle = {16, 7, 340, 29};
    DrawTextW(hdc, L"BOT KEEP STOCK v6.1", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, g_hFontHeaderSub);
    SetTextColor(hdc, RGB(148, 163, 184));
    RECT rcSub = {16, 29, 360, 47};
    DrawTextW(hdc, L"Native C++17 • Multi-Browser Paralel • Smart Table", -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    // 4 Kartu Counter Statistik Real-Time di sebelah kanan Header
    int pill_w = 68;
    int pill_h = 38;
    int pill_gap = 6;
    int pills_total_w = (pill_w * 4) + (pill_gap * 3);
    int pills_start_x = std::max(275, client_w - 106 - pills_total_w);
    int pill_y = 7;

    DrawStatPill(hdc, pills_start_x + (pill_w + pill_gap) * 0, pill_y, pill_w, pill_h, L"TOTAL",  g_statTotal,  RGB(96, 165, 250));
    DrawStatPill(hdc, pills_start_x + (pill_w + pill_gap) * 1, pill_y, pill_w, pill_h, L"SUKSES", g_statSukses, RGB(74, 222, 128));
    DrawStatPill(hdc, pills_start_x + (pill_w + pill_gap) * 2, pill_y, pill_w, pill_h, L"GAGAL",  g_statGagal,  RGB(248, 113, 113));
    DrawStatPill(hdc, pills_start_x + (pill_w + pill_gap) * 3, pill_y, pill_w, pill_h, L"SISA",   g_statSisa,   RGB(251, 191, 36));

    // 3. Gambar 4 Kartu Modern
    DrawModernCard(hdc, g_rcCardData,     L"1. SUMBER DATA & TABEL INTERAKTIF (LIVE STATUS)");
    DrawModernCard(hdc, g_rcCardConfig,   L"2. KONFIGURASI TOKO, IDENTITAS PIC & KECEPATAN");
    DrawModernCard(hdc, g_rcCardProgress, L"3. STATUS PERGERAKAN BOT & ESTIMASI WAKTU");
    DrawModernCard(hdc, g_rcCardLog,      L"4. LIVE LOG AKTIVITAS");
}

// ============================================================================
// FUNGSI TABEL TERPADU, VALIDASI DUPLIKAT & INLINE EDIT
// ============================================================================
static bool IsValidPositiveNumber(const std::string& raw) {
    std::string s = TrimString(raw);
    if (s.empty()) return false;
    bool has_digit = false;
    bool has_dot = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isdigit(c)) {
            has_digit = true;
        } else if ((c == '.' || c == ',') && !has_dot) {
            has_dot = true;
        } else {
            return false;
        }
    }
    if (!has_digit) return false;
    try {
        double v = std::stod(s);
        return v > 0.0;
    } catch (...) {
        return false;
    }
}

static std::string MakeRowDuplicateKey(const XlsxReader::StockRow& r) {
    std::string k = XlsxReader::BersihkanAngka(r.keep_stock) + "|" +
                    XlsxReader::BersihkanAngka(r.sku) + "|" +
                    XlsxReader::BersihkanAngka(r.gondola);
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    return k;
}

static void PerbaruiNomorUrutTabel() {
    int count = ListView_GetItemCount(g_treeData);
    for (int i = 0; i < count; ++i) {
        std::wstring num = std::to_wstring(i + 1);
        ListView_SetItemText(g_treeData, i, 0, const_cast<LPWSTR>(num.c_str()));
    }

    std::wstring info = L"Total: " + std::to_wstring(count) + L" baris";
    SetWindowTextW(g_lblInfoTabel, info.c_str());

    int max_val = std::max(1, count);
    SendMessageW(g_spinUpDownMulai, UDM_SETRANGE32, 1, max_val);

    RecalculateDashboardStats();
}

static void InsertRowToTable(const std::string& keep, const std::string& sku,
                             const std::string& qty, const std::string& gondola,
                             const std::string& status = "Menunggu") {
    int idx = ListView_GetItemCount(g_treeData);
    std::wstring wnum = std::to_wstring(idx + 1);
    std::wstring wkeep = Utf8ToWide(keep);
    std::wstring wsku = Utf8ToWide(sku);
    std::wstring wqty = Utf8ToWide(qty);
    std::wstring wgondola = Utf8ToWide(gondola);
    std::wstring wstatus = Utf8ToWide(status);

    LVITEMW lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = idx;
    lvi.iSubItem = 0;
    lvi.pszText = const_cast<LPWSTR>(wnum.c_str());
    ListView_InsertItem(g_treeData, &lvi);

    ListView_SetItemText(g_treeData, idx, 1, const_cast<LPWSTR>(wkeep.c_str()));
    ListView_SetItemText(g_treeData, idx, 2, const_cast<LPWSTR>(wsku.c_str()));
    ListView_SetItemText(g_treeData, idx, 3, const_cast<LPWSTR>(wqty.c_str()));
    ListView_SetItemText(g_treeData, idx, 4, const_cast<LPWSTR>(wgondola.c_str()));
    ListView_SetItemText(g_treeData, idx, 5, const_cast<LPWSTR>(wstatus.c_str()));
}

static void MuatDataKeTabel(const std::vector<XlsxReader::StockRow>& rows) {
    if (!g_treeData) return;
    SendMessageW(g_treeData, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_treeData);
    for (const auto& r : rows) {
        InsertRowToTable(
            XlsxReader::BersihkanAngka(r.keep_stock),
            XlsxReader::BersihkanAngka(r.sku),
            XlsxReader::BersihkanAngka(r.sku_qty),
            XlsxReader::BersihkanAngka(r.gondola),
            "Menunggu");
    }
    SendMessageW(g_treeData, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_treeData, nullptr, TRUE);
    PerbaruiNomorUrutTabel();
}

static void LoadExcelFileFromPath(const std::wstring& wpath) {
    if (g_isRunning.load()) return;

    std::string path_utf8 = WideToUtf8(wpath);
    std::wstring base_name = PathFindFileNameW(wpath.c_str());

    std::vector<XlsxReader::StockRow> rows;
    std::string err;
    if (XlsxReader::ReadExcelFile(path_utf8, rows, err)) {
        g_filepathExcel = path_utf8;
        int jml = static_cast<int>(rows.size());
        std::wstring lbl = L"File: " + base_name + L" (" + std::to_wstring(jml) + L" baris)";
        g_colFileLabel = g_isDarkMode ? RGB(74, 222, 128) : RGB(22, 163, 74);
        SetWindowTextW(g_lblFileTerpilih, lbl.c_str());
        InvalidateRect(g_lblFileTerpilih, nullptr, TRUE);

        MuatDataKeTabel(rows);
        g_failedRowIndices.clear();
        UpdateRetryFailedButtonState();

        SendMessageW(g_spinUpDownMulai, UDM_SETPOS32, 0, 1);
        TulisLog("File Excel dimuat ke Tabel: " + WideToUtf8(base_name) +
                 " (" + std::to_string(jml) + " baris).", "SUKSES");
    } else {
        std::wstring werr = L"Gagal membaca file Excel:\n" + Utf8ToWide(err);
        MessageBoxW(g_hMainWnd, werr.c_str(), L"Error Baca Excel", MB_OK | MB_ICONERROR);
    }
}

static void PasteDariClipboard() {
    if (g_isRunning) return;

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_hMainWnd)) {
        MessageBoxW(g_hMainWnd,
                    L"Tidak ada data di clipboard! Silakan blok dan Copy (Ctrl+C) sel di Excel terlebih dahulu.",
                    L"Clipboard Kosong", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring clip_wstr;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(hData));
        if (ptr) {
            clip_wstr = ptr;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();

    if (clip_wstr.empty()) {
        MessageBoxW(g_hMainWnd,
                    L"Tidak ada data di clipboard! Silakan blok dan Copy (Ctrl+C) sel di Excel terlebih dahulu.",
                    L"Clipboard Kosong", MB_OK | MB_ICONWARNING);
        return;
    }

    std::string clip_utf8 = WideToUtf8(clip_wstr);
    std::istringstream iss(clip_utf8);
    std::string line;
    int jumlah_masuk = 0;

    SendMessageW(g_treeData, WM_SETREDRAW, FALSE, 0);

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (TrimString(line).empty()) continue;

        char delim = (line.find('\t') != std::string::npos) ? '\t' : ';';
        std::vector<std::string> kolom;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, delim)) {
            kolom.push_back(XlsxReader::BersihkanAngka(item));
        }

        // Skip otomatis jika header ikut ter-copy
        if (kolom.size() >= 2) {
            std::string k0 = kolom[0], k1 = kolom[1];
            std::transform(k0.begin(), k0.end(), k0.begin(), ::tolower);
            std::transform(k1.begin(), k1.end(), k1.begin(), ::tolower);
            if (k0 == "alamat keep stock" || k1 == "sku") {
                continue;
            }
        }

        while (kolom.size() < 4) {
            kolom.push_back("");
        }

        if (kolom[0].empty() && kolom[1].empty() && kolom[2].empty() && kolom[3].empty()) {
            continue;
        }

        InsertRowToTable(kolom[0], kolom[1], kolom[2], kolom[3], "Menunggu");
        jumlah_masuk++;
    }

    SendMessageW(g_treeData, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_treeData, nullptr, TRUE);

    PerbaruiNomorUrutTabel();
    if (jumlah_masuk > 0) {
        g_colFileLabel = g_isDarkMode ? RGB(96, 165, 250) : RGB(37, 99, 235);
        std::wstring lbl = L"Sumber: Clipboard Excel (+" + std::to_wstring(jumlah_masuk) + L" baris)";
        SetWindowTextW(g_lblFileTerpilih, lbl.c_str());
        InvalidateRect(g_lblFileTerpilih, nullptr, TRUE);
        TulisLog("Berhasil Paste " + std::to_string(jumlah_masuk) + " baris data dari Excel ke tabel.", "SUKSES");
    } else {
        MessageBoxW(g_hMainWnd, L"Tidak ada baris data valid yang ditemukan di clipboard.",
                    L"Info Paste", MB_OK | MB_ICONINFORMATION);
    }
}

static void TambahBarisManual() {
    if (g_isRunning) return;

    std::string v_keep = TrimString(GetWindowTextUtf8(g_entKeep));
    std::string v_sku = TrimString(GetWindowTextUtf8(g_entSku));
    std::string v_qty = TrimString(GetWindowTextUtf8(g_entQty));
    std::string v_gondola = TrimString(GetWindowTextUtf8(g_entGondola));

    if (v_keep.empty() && v_sku.empty() && v_qty.empty() && v_gondola.empty()) {
        MessageBoxW(g_hMainWnd, L"Isi minimal salah satu kolom sebelum menekan tombol Tambah!",
                    L"Kosong", MB_OK | MB_ICONWARNING);
        return;
    }

    InsertRowToTable(v_keep, v_sku, v_qty, v_gondola, "Menunggu");
    PerbaruiNomorUrutTabel();

    SetWindowTextW(g_entKeep, L"");
    SetWindowTextW(g_entSku, L"");
    SetWindowTextW(g_entQty, L"");
    SetWindowTextW(g_entGondola, L"");
    SetFocus(g_entKeep);
}

static void HapusBarisTerpilih() {
    if (g_isRunning) return;

    int sel_count = ListView_GetSelectedCount(g_treeData);
    if (sel_count <= 0) {
        MessageBoxW(g_hMainWnd, L"Klik salah satu (atau beberapa) baris di tabel yang ingin dihapus!",
                    L"Pilih Baris", MB_OK | MB_ICONINFORMATION);
        return;
    }

    int total = ListView_GetItemCount(g_treeData);
    for (int i = total - 1; i >= 0; --i) {
        if (ListView_GetItemState(g_treeData, i, LVIS_SELECTED) & LVIS_SELECTED) {
            ListView_DeleteItem(g_treeData, i);
        }
    }
    g_failedRowIndices.clear();
    UpdateRetryFailedButtonState();
    PerbaruiNomorUrutTabel();
}

static void KosongkanTabel() {
    if (g_isRunning) return;
    if (ListView_GetItemCount(g_treeData) == 0) return;

    if (MessageBoxW(g_hMainWnd, L"Kosongkan seluruh baris pada tabel input?",
                    L"Konfirmasi", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        ListView_DeleteAllItems(g_treeData);
        g_filepathExcel.clear();
        g_failedRowIndices.clear();
        UpdateRetryFailedButtonState();
        PerbaruiNomorUrutTabel();
        g_colFileLabel = ThemeTextMuted();
        SetWindowTextW(g_lblFileTerpilih, L"Siap: Buka/Drop file .xlsx atau Paste Excel");
        InvalidateRect(g_lblFileTerpilih, nullptr, TRUE);
        TulisLog("Tabel data telah dikosongkan.", "WARN");
    }
}

static std::vector<XlsxReader::StockRow> AmbilDataDariTabel() {
    std::vector<XlsxReader::StockRow> list;
    int count = ListView_GetItemCount(g_treeData);
    list.reserve(count);

    wchar_t buf[512];
    auto get_cell = [&](int row, int col) -> std::string {
        buf[0] = L'\0';
        ListView_GetItemText(g_treeData, row, col, buf, 512);
        return WideToUtf8(buf);
    };

    for (int i = 0; i < count; ++i) {
        XlsxReader::StockRow sr;
        sr.keep_stock = get_cell(i, 1);
        sr.sku        = get_cell(i, 2);
        sr.sku_qty    = get_cell(i, 3);
        sr.gondola    = get_cell(i, 4);
        list.push_back(std::move(sr));
    }
    return list;
}

static void CekDuplikatDanValidasiKlik() {
    if (g_isRunning) return;
    auto rows = AmbilDataDariTabel();
    if (rows.empty()) {
        MessageBoxW(g_hMainWnd, L"Tabel masih kosong! Buka file Excel atau Paste data terlebih dahulu.",
                    L"Tabel Kosong", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::map<std::string, int> first_seen;
    std::vector<int> dup_indices;
    std::vector<int> empty_indices;
    std::vector<int> bad_qty_indices;

    SendMessageW(g_treeData, WM_SETREDRAW, FALSE, 0);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[i];
        std::string k_keep = XlsxReader::BersihkanAngka(r.keep_stock);
        std::string k_sku  = XlsxReader::BersihkanAngka(r.sku);
        std::string k_qty  = XlsxReader::BersihkanAngka(r.sku_qty);
        std::string k_gon  = XlsxReader::BersihkanAngka(r.gondola);

        std::wstring st = L"Siap";
        if (k_keep.empty() || k_sku.empty() || k_qty.empty() || k_gon.empty()) {
            empty_indices.push_back(i);
            st = L"! Kolom Kosong";
        } else if (!IsValidPositiveNumber(k_qty)) {
            bad_qty_indices.push_back(i);
            st = L"! QTY Invalid";
        } else {
            std::string key = MakeRowDuplicateKey(r);
            auto it = first_seen.find(key);
            if (it != first_seen.end()) {
                dup_indices.push_back(i);
                st = L"! Duplikat (#" + std::to_wstring(it->second + 1) + L")";
            } else {
                first_seen[key] = i;
            }
        }
        ListView_SetItemText(g_treeData, i, 5, const_cast<LPWSTR>(st.c_str()));
    }
    SendMessageW(g_treeData, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_treeData, nullptr, TRUE);
    RecalculateDashboardStats();

    if (dup_indices.empty() && empty_indices.empty() && bad_qty_indices.empty()) {
        TulisLog("Validasi Tabel: Semua " + std::to_string(rows.size()) + " baris valid & bebas duplikat.", "SUKSES");
        MessageBoxW(g_hMainWnd,
                    (L"Semua " + std::to_wstring(rows.size()) + L" baris data valid, angka QTY sesuai, dan tidak ada baris duplikat!").c_str(),
                    L"Validasi Data OK", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wostringstream report;
    report << L"Hasil Pemeriksaan Tabel (" << rows.size() << L" baris):\n\n";
    if (!dup_indices.empty()) {
        report << L"• Baris Duplikat: " << dup_indices.size() << L" baris\n";
    }
    if (!empty_indices.empty()) {
        report << L"• Baris Kolom Kosong: " << empty_indices.size() << L" baris\n";
    }
    if (!bad_qty_indices.empty()) {
        report << L"• Baris SKU QTY Bukan Angka: " << bad_qty_indices.size() << L" baris\n";
    }
    report << L"\nBaris bermasalah telah ditandai pada kolom Status.";

    if (!dup_indices.empty()) {
        report << L"\n\nApakah Anda ingin MENGHAPUS " << dup_indices.size() << L" baris duplikat secara otomatis sekarang?";
        if (MessageBoxW(g_hMainWnd, report.str().c_str(), L"Deteksi Duplikat & Validasi", MB_YESNO | MB_ICONWARNING) == IDYES) {
            SendMessageW(g_treeData, WM_SETREDRAW, FALSE, 0);
            for (int idx = static_cast<int>(dup_indices.size()) - 1; idx >= 0; --idx) {
                ListView_DeleteItem(g_treeData, dup_indices[idx]);
            }
            SendMessageW(g_treeData, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_treeData, nullptr, TRUE);
            PerbaruiNomorUrutTabel();
            TulisLog("Berhasil menghapus " + std::to_string(dup_indices.size()) + " baris duplikat dari tabel.", "WARN");
        }
    } else {
        MessageBoxW(g_hMainWnd, report.str().c_str(), L"Hasil Validasi Data", MB_OK | MB_ICONWARNING);
    }
}

// Simpan perubahan Inline Edit pada sel Treeview/ListView
static void CommitInlineEdit(bool save) {
    if (!g_hInlineEdit) return;
    HWND hEdit = g_hInlineEdit;
    g_hInlineEdit = nullptr;

    if (save && g_editRow >= 0 && g_editCol >= 1 && g_editCol <= 4) {
        std::string new_val = TrimString(GetWindowTextUtf8(hEdit));
        std::wstring wval = Utf8ToWide(new_val);
        ListView_SetItemText(g_treeData, g_editRow, g_editCol, const_cast<LPWSTR>(wval.c_str()));
        wchar_t wready[] = L"Menunggu";
        ListView_SetItemText(g_treeData, g_editRow, 5, wready);
        RecalculateDashboardStats();
    }
    g_editRow = -1;
    g_editCol = -1;
    DestroyWindow(hEdit);
}

static LRESULT CALLBACK InlineEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            CommitInlineEdit(true);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            CommitInlineEdit(false);
            return 0;
        }
    } else if (msg == WM_KILLFOCUS) {
        CommitInlineEdit(true);
        return 0;
    }
    return CallWindowProcW(g_origInlineEditProc, hWnd, msg, wParam, lParam);
}

static void MulaiEditSelTabel(int row, int col) {
    if (g_isRunning || row < 0 || col < 1 || col > 4) return;
    CommitInlineEdit(true);

    RECT rcCell = {0};
    ListView_GetSubItemRect(g_treeData, row, col, LVIR_BOUNDS, &rcCell);

    wchar_t cur_text[512] = {0};
    ListView_GetItemText(g_treeData, row, col, cur_text, 512);

    g_editRow = row;
    g_editCol = col;
    g_hInlineEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", cur_text,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER,
        rcCell.left, rcCell.top,
        rcCell.right - rcCell.left, rcCell.bottom - rcCell.top + 2,
        g_treeData, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INLINE_EDIT)), g_hInst, nullptr);

    SendMessageW(g_hInlineEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontReg9), TRUE);
    SendMessageW(g_hInlineEdit, EM_SETSEL, 0, -1);
    SetFocus(g_hInlineEdit);

    g_origInlineEditProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hInlineEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InlineEditSubclassProc)));
}

static LRESULT CALLBACK GondolaEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        TambahBarisManual();
        return 0;
    }
    return CallWindowProcW(g_origGondolaEditProc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ListViewSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_DELETE) {
            HapusBarisTerpilih();
            return 0;
        }
        if ((wParam == 'V' || wParam == 'v') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            PasteDariClipboard();
            return 0;
        }
    }
    return CallWindowProcW(g_origListViewProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// LAYOUT RESPONSIVE & AUTO-STRETCH KOLOM TABEL
// ============================================================================
static void UpdateLayout(int client_w, int client_h) {
    if (client_w <= 0 || client_h <= 0) return;

    // Tombol Toggle Tema di Kanan Atas Header
    MoveWindow(g_btnTheme, client_w - 98, 13, 84, 28, TRUE);

    int margin_x = 14;
    int box_w = client_w - (margin_x * 2);
    int cur_y = 62;

    // 1. Kartu Sumber Data & Tabel Interaktif Terpadu
    int data_card_h = 236;
    g_rcCardData = {margin_x, cur_y, margin_x + box_w, cur_y + data_card_h};

    int inner_x = margin_x + 12;
    int inner_w = box_w - 24;

    int row1_y = cur_y + 32;
    int bx = inner_x;
    MoveWindow(g_btnPilihFile,   bx, row1_y, 118, 25, TRUE); bx += 123;
    MoveWindow(g_btnPasteClip,   bx, row1_y, 128, 25, TRUE); bx += 133;
    MoveWindow(g_btnCekValidasi, bx, row1_y, 132, 25, TRUE); bx += 137;
    MoveWindow(g_btnHapusBaris,  bx, row1_y, 95,  25, TRUE); bx += 100;
    MoveWindow(g_btnResetTabel,  bx, row1_y, 68,  25, TRUE); bx += 74;
    MoveWindow(g_lblFileTerpilih, bx, row1_y + 4, std::max(60, (inner_x + inner_w) - bx), 18, TRUE);

    int row2_y = cur_y + 63;
    int x = inner_x;
    MoveWindow(g_lblKeep,    x, row2_y + 3, 34, 18, TRUE); x += 35;
    MoveWindow(g_entKeep,    x, row2_y,     88, 22, TRUE); x += 94;
    MoveWindow(g_lblSku,     x, row2_y + 3, 28, 18, TRUE); x += 29;
    MoveWindow(g_entSku,     x, row2_y,     96, 22, TRUE); x += 102;
    MoveWindow(g_lblQty,     x, row2_y + 3, 28, 18, TRUE); x += 29;
    MoveWindow(g_entQty,     x, row2_y,     56, 22, TRUE); x += 62;
    MoveWindow(g_lblGondola, x, row2_y + 3, 48, 18, TRUE); x += 49;
    MoveWindow(g_entGondola, x, row2_y,     88, 22, TRUE); x += 94;
    MoveWindow(g_btnTambahM, x, row2_y - 1, 74, 24, TRUE); x += 80;
    MoveWindow(g_lblInfoTabel, x, row2_y + 3, std::max(60, (inner_x + inner_w) - x), 18, TRUE);

    int tree_y = cur_y + 91;
    int tree_h = 122;
    MoveWindow(g_treeData, inner_x, tree_y, inner_w, tree_h, TRUE);
    MoveWindow(g_lblTips,  inner_x, tree_y + tree_h + 4, inner_w, 16, TRUE);

    // Auto-stretch lebar kolom tabel secara proporsional
    int avail_col_w = std::max(540, inner_w - 22);
    int w_no     = 42;
    int w_qty    = std::max(68, (avail_col_w * 11) / 100);
    int w_status = std::max(135, (avail_col_w * 23) / 100);
    int rem_w    = std::max(280, avail_col_w - w_no - w_qty - w_status);
    int w_each3  = rem_w / 3;
    ListView_SetColumnWidth(g_treeData, 0, w_no);
    ListView_SetColumnWidth(g_treeData, 1, w_each3);
    ListView_SetColumnWidth(g_treeData, 2, w_each3);
    ListView_SetColumnWidth(g_treeData, 3, w_qty);
    ListView_SetColumnWidth(g_treeData, 4, rem_w - (w_each3 * 2));
    ListView_SetColumnWidth(g_treeData, 5, w_status);

    cur_y += data_card_h + 8;

    // 2. Kartu Konfigurasi Toko, Identitas PIC & Kecepatan
    int config_card_h = 112;
    g_rcCardConfig = {margin_x, cur_y, margin_x + box_w, cur_y + config_card_h};

    int col1_lbl_x = margin_x + 12;
    int col1_ctl_x = margin_x + 102;
    int col2_lbl_x = margin_x + (box_w / 2) + 8;
    int col2_ctl_x = col2_lbl_x + 76;

    int r0_y = cur_y + 30;
    int r1_y = cur_y + 57;
    int r2_y = cur_y + 83;

    MoveWindow(g_lblPilihToko,  col1_lbl_x,       r0_y + 3, 86,  18, TRUE);
    MoveWindow(g_comboToko,     col1_ctl_x,       r0_y,     106, 200, TRUE);
    MoveWindow(g_btnKelolaToko, col1_ctl_x + 112, r0_y - 1, 92,  24, TRUE);

    MoveWindow(g_lblJenisForm, col1_lbl_x, r1_y + 3, 86,  18, TRUE);
    MoveWindow(g_comboJenis,   col1_ctl_x, r1_y,     204, 200, TRUE);

    MoveWindow(g_lblMulaiData,    col1_lbl_x,       r2_y + 3, 86, 18, TRUE);
    MoveWindow(g_spinEditMulai,   col1_ctl_x,       r2_y,     50, 22, TRUE);
    MoveWindow(g_spinUpDownMulai, col1_ctl_x + 50,  r2_y,     17, 22, TRUE);
    MoveWindow(g_lblMaxRetry,     col1_ctl_x + 74,  r2_y + 3, 72, 18, TRUE);
    MoveWindow(g_spinEditRetry,   col1_ctl_x + 148, r2_y,     39, 22, TRUE);
    MoveWindow(g_spinUpDownRetry, col1_ctl_x + 187, r2_y,     17, 22, TRUE);

    int right_ctl_w = std::max(150, (margin_x + box_w - 12) - col2_ctl_x);
    MoveWindow(g_lblNamaPic,  col2_lbl_x, r0_y + 3, 72, 18, TRUE);
    MoveWindow(g_entryNama,   col2_ctl_x, r0_y,     right_ctl_w, 22, TRUE);

    MoveWindow(g_lblPosisi,   col2_lbl_x, r1_y + 3, 72, 18, TRUE);
    MoveWindow(g_comboPosisi, col2_ctl_x, r1_y,     right_ctl_w, 200, TRUE);

    MoveWindow(g_lblWorker,   col2_lbl_x, r2_y + 3, 72, 18, TRUE);
    int worker_combo_w = std::min(150, std::max(115, right_ctl_w - 86));
    MoveWindow(g_comboWorker, col2_ctl_x, r2_y, worker_combo_w, 200, TRUE);
    MoveWindow(g_chkHeadless, col2_ctl_x + worker_combo_w + 8, r2_y + 2, 80, 20, TRUE);

    cur_y += config_card_h + 8;

    // 3. Kartu Status Pergerakan & Progress Bar
    int prog_card_h = 68;
    g_rcCardProgress = {margin_x, cur_y, margin_x + box_w, cur_y + prog_card_h};
    int half_w = (box_w - 24) / 2;
    MoveWindow(g_lblStatus,   margin_x + 12,               cur_y + 27, half_w + 30, 18, TRUE);
    MoveWindow(g_lblEta,      margin_x + 12 + half_w + 35, cur_y + 27, half_w - 35, 18, TRUE);
    MoveWindow(g_progressBar, margin_x + 12,               cur_y + 46, box_w - 24,  14, TRUE);
    cur_y += prog_card_h + 8;

    // 4. Deretan Tombol Kontrol Utama (Tengah)
    int btn_y = cur_y;
    int total_btn_w = 142 + 8 + 108 + 8 + 92 + 8 + 154;
    int btn_start_x = std::max(margin_x, (client_w - total_btn_w) / 2);
    MoveWindow(g_btnStart,       btn_start_x,       btn_y, 142, 30, TRUE);
    MoveWindow(g_btnPause,       btn_start_x + 150, btn_y, 108, 30, TRUE);
    MoveWindow(g_btnStop,        btn_start_x + 266, btn_y, 92,  30, TRUE);
    MoveWindow(g_btnRetryFailed, btn_start_x + 366, btn_y, 154, 30, TRUE);
    cur_y += 38;

    // 5. Kartu Live Log Aktivitas (Mengisi Sisa Ruang Bawah)
    int log_card_h = std::max(95, client_h - cur_y - 10);
    g_rcCardLog = {margin_x, cur_y, margin_x + box_w, cur_y + log_card_h};
    MoveWindow(g_btnClearLog, margin_x + box_w - 98, cur_y + 7,  86, 22, TRUE);
    MoveWindow(g_richLog,     margin_x + 12,         cur_y + 33, box_w - 24, log_card_h - 42, TRUE);

    InvalidateRect(g_hMainWnd, nullptr, TRUE);
}

static void ApplyTheme() {
    RebuildThemeBrushes();
    SetWindowDarkTitleBar(g_hMainWnd, g_isDarkMode);

    if (g_btnTheme) {
        SetWindowTextW(g_btnTheme, g_isDarkMode ? L"☀ Light" : L"🌙 Dark");
    }

    if (g_treeData) {
        ListView_SetBkColor(g_treeData, ThemeTableBg());
        ListView_SetTextBkColor(g_treeData, ThemeTableBg());
        ListView_SetTextColor(g_treeData, ThemeTextPrimary());
    }

    if (g_richLog) {
        SendMessageW(g_richLog, EM_SETBKGNDCOLOR, 0, g_isDarkMode ? RGB(9, 14, 26) : RGB(24, 24, 27));
    }

    if (g_filepathExcel.empty() && ListView_GetItemCount(g_treeData) == 0) {
        g_colFileLabel = ThemeTextMuted();
    }
    if (!g_isRunning.load()) {
        g_colStatusLabel = ThemeTextMuted();
    }

    if (g_hMainWnd) {
        RedrawWindow(g_hMainWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

static void SaveCurrentUserPrefs() {
    JsonConfig::UserPrefs prefs;
    prefs.toko = TrimString(GetWindowTextUtf8(g_comboToko));
    prefs.jenis = TrimString(GetWindowTextUtf8(g_comboJenis));
    prefs.nama = TrimString(GetWindowTextUtf8(g_entryNama));
    prefs.posisi = TrimString(GetWindowTextUtf8(g_comboPosisi));
    prefs.headless = (SendMessageW(g_chkHeadless, BM_GETCHECK, 0, 0) == BST_CHECKED);
    prefs.mode_sumber = "paste";
    int w_sel = static_cast<int>(SendMessageW(g_comboWorker, CB_GETCURSEL, 0, 0));
    prefs.jumlah_worker = (w_sel >= 0 && w_sel < 4) ? (w_sel + 1) : 1;
    try {
        prefs.max_retry = std::clamp(std::stoi(TrimString(GetWindowTextUtf8(g_spinEditRetry))), 1, 10);
    } catch (...) {
        prefs.max_retry = 3;
    }
    prefs.dark_mode = g_isDarkMode;
    JsonConfig::SaveUserPrefs(FILE_PREFERENSI, prefs);
}

// ============================================================================
// FUNGSI BOT WORKER THREAD (MULTI-BROWSER PARALEL & BATAS RETRY OTOMATIS)
// ============================================================================
static bool CekStatusKontrol(int nomor_data) {
    while (g_isPaused.load() && !g_isStopped.load()) {
        SetStatusAsync(
            "Dijeda sebelum data ke-" + std::to_string(nomor_data) + ". Klik 'LANJUTKAN'...",
            RGB(0xFF, 0x98, 0x00));
        Sleep(250);
    }
    return g_isStopped.load();
}

static void ProsesSeleniumMasterThread(std::vector<XlsxReader::StockRow> df,
                                       std::vector<int> target_indices,
                                       std::string link_form,
                                       std::string nama_identitas,
                                       std::string posisi_identitas,
                                       bool mode_headless,
                                       int num_workers,
                                       int max_retry) {
    int total_data = static_cast<int>(df.size());
    int total_target = static_cast<int>(target_indices.size());
    int actual_workers = std::clamp(num_workers, 1, std::max(1, std::min(4, total_target)));

    std::atomic<size_t> next_queue_pos{0};
    std::atomic<int> completed_count{0};
    std::atomic<int> success_count{0};
    std::atomic<int> last_success_row{0};
    std::atomic<bool> any_driver_started{false};

    std::mutex stats_mutex;
    std::vector<int> failed_rows;
    std::string fatal_init_error;

    auto waktu_mulai_sesi = std::chrono::steady_clock::now();

    std::string mode_teks = mode_headless ? "Headless" : "Tampil Layar";
    TulisLog("Menyiapkan " + std::to_string(actual_workers) + " Browser [" + mode_teks +
             "] | Target: " + std::to_string(total_target) + " baris | Maks Retry: " +
             std::to_string(max_retry) + "x...", "INFO");

    SetProgressAsync(0, total_target * 2);
    SetEtaAsync("Target: " + std::to_string(total_target) + " baris (" +
                std::to_string(actual_workers) + " worker) | Estimasi: Menghitung...");

    auto worker_fn = [&](int worker_id) {
        std::string w_prefix = (actual_workers > 1) ? ("[W" + std::to_string(worker_id) + "] ") : "";
        if (worker_id > 1) {
            Sleep((worker_id - 1) * 350);
        }
        if (g_isStopped.load()) return;

        WebDriver::ChromeSession driver;
        try {
            driver.Start(mode_headless, [&](const std::string& msg) {
                TulisLog(w_prefix + msg, "INFO");
            });
            any_driver_started.store(true);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(stats_mutex);
            if (fatal_init_error.empty()) {
                fatal_init_error = e.what();
            }
            TulisLog(w_prefix + "Gagal membuka Chrome: " + e.what(), "ERROR");
            return;
        }

        bool perlu_buka_link_awal = true;

        while (!g_isStopped.load()) {
            size_t q_pos = next_queue_pos.fetch_add(1);
            if (q_pos >= target_indices.size()) {
                break;
            }

            int index = target_indices[q_pos];
            int nomor_data = index + 1;

            if (CekStatusKontrol(nomor_data)) {
                break;
            }

            bool berhasil = false;
            int percobaan = 1;
            auto waktu_mulai_baris = std::chrono::steady_clock::now();

            std::string keep_saat_ini    = XlsxReader::BersihkanAngka(df[index].keep_stock);
            std::string sku_saat_ini     = XlsxReader::BersihkanAngka(df[index].sku);
            std::string qty_saat_ini     = XlsxReader::BersihkanAngka(df[index].sku_qty);
            std::string gondola_saat_ini = XlsxReader::BersihkanAngka(df[index].gondola);

            while (!berhasil && percobaan <= max_retry) {
                if (CekStatusKontrol(nomor_data)) {
                    break;
                }

                try {
                    if (percobaan > 1) {
                        std::string retry_status = w_prefix + "Retry " + std::to_string(percobaan) + "/" + std::to_string(max_retry) + "...";
                        SetRowStatusAsync(index, retry_status, true);
                        SetStatusAsync(
                            w_prefix + "Mencoba ulang baris " + std::to_string(nomor_data) +
                            " (" + std::to_string(percobaan) + "/" + std::to_string(max_retry) + ")...",
                            RGB(0xFF, 0x98, 0x00));
                        perlu_buka_link_awal = true;
                    } else {
                        std::string proc_status = (actual_workers > 1)
                            ? ("Memproses [W" + std::to_string(worker_id) + "]...")
                            : "Memproses...";
                        SetRowStatusAsync(index, proc_status, true);
                        SetStatusAsync(
                            w_prefix + "Memproses baris " + std::to_string(nomor_data) + "/" +
                            std::to_string(total_data) + " (SKU: " + sku_saat_ini + ")...",
                            RGB(59, 130, 246));
                    }

                    volatile bool stop_poll = g_isStopped.load();
                    if (perlu_buka_link_awal) {
                        driver.NavigateTo(link_form);
                    }

                    // --- LAYER 1 (IDENTITAS) ---
                    std::string kolom_nama = driver.WaitElementClickable("(//input[@type=\"text\"])[1]", 15000, &stop_poll);
                    driver.ClearElement(kolom_nama);
                    driver.SendKeysToElement(kolom_nama, nama_identitas);

                    std::string dropdown_posisi = driver.WaitElementClickable("(//div[@role='listbox'])[1]", 15000, &stop_poll);
                    driver.ClickElement(dropdown_posisi);

                    std::string xpath_opsi = "//div[@role='option']//span[text()='" + posisi_identitas + "']";
                    std::string opsi_posisi = driver.WaitElementClickable(xpath_opsi, 15000, &stop_poll);
                    Sleep(120);
                    driver.ClickElement(opsi_posisi);

                    Sleep(120);
                    std::string tombol_next = driver.WaitElementClickable("//span[text()='Berikutnya' or text()='Next']", 15000, &stop_poll);
                    driver.ClickElement(tombol_next);

                    // --- LAYER 2 (DATA STOK) ---
                    std::string kolom_gondola = driver.WaitElementClickable("(//input[@type=\"text\"])[4]", 15000, &stop_poll);

                    std::string kolom_keep = driver.FindElementByXPath("(//input[@type=\"text\"])[1]");
                    driver.ClearElement(kolom_keep);
                    driver.SendKeysToElement(kolom_keep, keep_saat_ini);

                    std::string kolom_sku = driver.FindElementByXPath("(//input[@type=\"text\"])[2]");
                    driver.ClearElement(kolom_sku);
                    driver.SendKeysToElement(kolom_sku, sku_saat_ini);

                    std::string kolom_qty = driver.FindElementByXPath("(//input[@type=\"text\"])[3]");
                    driver.ClearElement(kolom_qty);
                    driver.SendKeysToElement(kolom_qty, qty_saat_ini);

                    driver.ClearElement(kolom_gondola);
                    driver.SendKeysToElement(kolom_gondola, gondola_saat_ini);

                    if (g_isStopped.load()) {
                        break;
                    }

                    std::string tombol_kirim = driver.WaitElementClickable("//span[text()='Kirim' or text()='Submit']", 15000, &stop_poll);
                    driver.ClickElement(tombol_kirim);

                    driver.WaitElementInvisible("//span[text()='Kirim' or text()='Submit']", 15000, &stop_poll);

                    try {
                        std::string link_lagi = driver.WaitElementClickable(
                            "//a[contains(@href, 'viewform') or contains(@href, 'form_confirm')]", 4000, &stop_poll);
                        driver.ClickElement(link_lagi);
                        perlu_buka_link_awal = false;
                    } catch (...) {
                        perlu_buka_link_awal = true;
                    }

                    berhasil = true;
                    success_count.fetch_add(1);
                    int done_now = completed_count.fetch_add(1) + 1;

                    int prev_last = last_success_row.load();
                    while (nomor_data > prev_last && !last_success_row.compare_exchange_weak(prev_last, nomor_data)) {}

                    SetProgressAsync(done_now * 2, total_target * 2);

                    auto waktu_selesai_baris = std::chrono::steady_clock::now();
                    double durasi_baris = std::chrono::duration<double>(waktu_selesai_baris - waktu_mulai_baris).count();
                    double elapsed_sesi = std::chrono::duration<double>(waktu_selesai_baris - waktu_mulai_sesi).count();
                    double rata_efektif = elapsed_sesi / static_cast<double>(std::max(1, done_now));
                    int sisa_baris = std::max(0, total_target - done_now);
                    double estimasi_detik = sisa_baris * rata_efektif;

                    std::ostringstream oss_dur;
                    oss_dur << std::fixed << std::setprecision(1) << durasi_baris;

                    SetRowStatusAsync(index, "✓ Sukses (" + oss_dur.str() + "s)", false);

                    if (sisa_baris > 0) {
                        std::ostringstream oss_eff;
                        oss_eff << std::fixed << std::setprecision(1) << rata_efektif;
                        SetEtaAsync("Rata2: " + oss_eff.str() + " dtk/baris | Sisa: " +
                                    std::to_string(sisa_baris) + " baris | ETA: ~" + FormatWaktu(estimasi_detik));
                    } else {
                        SetEtaAsync("Sisa: 0 baris | Estimasi sisa waktu: Selesai!");
                    }

                    TulisLog(w_prefix + "v Baris " + std::to_string(nomor_data) + "/" + std::to_string(total_data) +
                             " SUKSES -> SKU: " + sku_saat_ini + " (Qty: " + qty_saat_ini + ") [" +
                             oss_dur.str() + "s]", "SUKSES");

                } catch (const std::exception& e) {
                    if (g_isStopped.load()) {
                        break;
                    }
                    std::string err = e.what();
                    if (err.size() > 60) err = err.substr(0, 60);
                    TulisLog(w_prefix + "! Gagal baris " + std::to_string(nomor_data) +
                             " (Percobaan " + std::to_string(percobaan) + "/" + std::to_string(max_retry) +
                             "): " + err + "...", "ERROR");
                    percobaan++;
                    perlu_buka_link_awal = true;
                    if (percobaan <= max_retry) {
                        Sleep(1500);
                    }
                }
            }

            if (!berhasil && !g_isStopped.load()) {
                int done_now = completed_count.fetch_add(1) + 1;
                SetProgressAsync(done_now * 2, total_target * 2);
                SetRowStatusAsync(index, "✗ Gagal (" + std::to_string(max_retry) + "x)", false);
                {
                    std::lock_guard<std::mutex> lk(stats_mutex);
                    failed_rows.push_back(index);
                }
                TulisLog(w_prefix + "X Baris " + std::to_string(nomor_data) + " GAGAL setelah " +
                         std::to_string(max_retry) + "x percobaan -> Dilewati ke baris berikutnya.", "ERROR");
            }
        }

        driver.Quit();
    };

    std::vector<std::thread> pool;
    pool.reserve(actual_workers);
    for (int w = 1; w <= actual_workers; ++w) {
        pool.emplace_back(worker_fn, w);
    }
    for (auto& t : pool) {
        if (t.joinable()) t.join();
    }

    std::sort(failed_rows.begin(), failed_rows.end());

    bool sys_err = (!any_driver_started.load() && !fatal_init_error.empty());
    auto* fin = new FinishedPayload{
        g_isStopped.load(),
        sys_err,
        last_success_row.load(),
        success_count.load(),
        total_data,
        std::move(failed_rows),
        Utf8ToWide(fatal_init_error)
    };
    PostMessageW(g_hMainWnd, WM_APP_FINISHED, 0, reinterpret_cast<LPARAM>(fin));
}

// ============================================================================
// DIALOG MODAL: KELOLA DATABASE TOKO (config_toko.json)
// ============================================================================
static HWND g_hTokoWin = nullptr;
static HWND g_hTokoList = nullptr;
static HWND g_hTokoEntKode = nullptr;
static HWND g_hTokoEntIn = nullptr;
static HWND g_hTokoEntOut = nullptr;

static void RefreshMainComboToko() {
    std::string cur = GetWindowTextUtf8(g_comboToko);
    SendMessageW(g_comboToko, CB_RESETCONTENT, 0, 0);
    int sel_idx = -1;
    int idx = 0;
    for (const auto& kv : g_databaseForm) {
        std::wstring wkode = Utf8ToWide(kv.first);
        SendMessageW(g_comboToko, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wkode.c_str()));
        if (kv.first == cur) sel_idx = idx;
        idx++;
    }
    if (sel_idx >= 0) {
        SendMessageW(g_comboToko, CB_SETCURSEL, sel_idx, 0);
    } else {
        SetWindowTextW(g_comboToko, L"Pilih Toko");
    }
}

static void RefreshTokoListbox() {
    if (!g_hTokoList) return;
    SendMessageW(g_hTokoList, LB_RESETCONTENT, 0, 0);
    for (const auto& kv : g_databaseForm) {
        std::wstring wkode = Utf8ToWide(kv.first);
        SendMessageW(g_hTokoList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wkode.c_str()));
    }
    RefreshMainComboToko();
}

static LRESULT CALLBACK TokoModalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND lblTitle = CreateWindowW(L"STATIC", L"DAFTAR CABANG TOKO & LINK FORM",
                WS_CHILD | WS_VISIBLE | SS_CENTER, 15, 10, 455, 22, hWnd, nullptr, g_hInst, nullptr);
            SendMessageW(lblTitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold9), TRUE);

            HWND lblList = CreateWindowW(L"STATIC", L"Toko Terdaftar:",
                WS_CHILD | WS_VISIBLE, 15, 36, 150, 18, hWnd, nullptr, g_hInst, nullptr);
            SendMessageW(lblList, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold9), TRUE);

            g_hTokoList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                15, 56, 455, 95, hWnd, reinterpret_cast<HMENU>(IDC_TOKO_LISTBOX), g_hInst, nullptr);
            SendMessageW(g_hTokoList, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontReg9), TRUE);

            HWND grpEdit = CreateWindowW(L"BUTTON", L" Tambah / Update Toko ",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 160, 455, 145, hWnd, nullptr, g_hInst, nullptr);
            SendMessageW(grpEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold9), TRUE);

            HWND lblKode = CreateWindowW(L"STATIC", L"Kode Toko :", WS_CHILD | WS_VISIBLE, 28, 188, 95, 18, hWnd, nullptr, g_hInst, nullptr);
            g_hTokoEntKode = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE,
                130, 185, 130, 22, hWnd, reinterpret_cast<HMENU>(IDC_TOKO_ENT_KODE), g_hInst, nullptr);
            HWND lblHint = CreateWindowW(L"STATIC", L"(Contoh: XBTK, XBDJ)", WS_CHILD | WS_VISIBLE, 270, 188, 180, 18, hWnd, nullptr, g_hInst, nullptr);

            HWND lblIn = CreateWindowW(L"STATIC", L"Link Form IN :", WS_CHILD | WS_VISIBLE, 28, 222, 95, 18, hWnd, nullptr, g_hInst, nullptr);
            g_hTokoEntIn = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                130, 219, 325, 22, hWnd, reinterpret_cast<HMENU>(IDC_TOKO_ENT_IN), g_hInst, nullptr);

            HWND lblOut = CreateWindowW(L"STATIC", L"Link Form OUT :", WS_CHILD | WS_VISIBLE, 28, 256, 95, 18, hWnd, nullptr, g_hInst, nullptr);
            g_hTokoEntOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                130, 253, 325, 22, hWnd, reinterpret_cast<HMENU>(IDC_TOKO_ENT_OUT), g_hInst, nullptr);

            for (HWND h : {lblKode, g_hTokoEntKode, lblIn, g_hTokoEntIn, lblOut, g_hTokoEntOut}) {
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontReg9), TRUE);
            }
            SendMessageW(lblHint, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontItalic8), TRUE);

            HWND btnSimpan = CreateWindowW(L"BUTTON", L"Simpan / Update Toko",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 75, 320, 165, 30, hWnd,
                reinterpret_cast<HMENU>(IDC_TOKO_BTN_SIMPAN), g_hInst, nullptr);
            HWND btnHapus = CreateWindowW(L"BUTTON", L"Hapus Toko Terpilih",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 250, 320, 165, 30, hWnd,
                reinterpret_cast<HMENU>(IDC_TOKO_BTN_HAPUS), g_hInst, nullptr);

            SendMessageW(btnSimpan, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold9), TRUE);
            SendMessageW(btnHapus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold9), TRUE);
            SubclassModernButton(btnSimpan);
            SubclassModernButton(btnHapus);

            RefreshTokoListbox();
            return 0;
        }

        case WM_DRAWITEM: {
            DrawColoredButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == IDC_TOKO_LISTBOX && code == LBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(g_hTokoList, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR) {
                    wchar_t wkode[128] = {0};
                    SendMessageW(g_hTokoList, LB_GETTEXT, sel, reinterpret_cast<LPARAM>(wkode));
                    std::string kode = WideToUtf8(wkode);
                    auto it = g_databaseForm.find(kode);
                    if (it != g_databaseForm.end()) {
                        SetWindowTextW(g_hTokoEntKode, wkode);
                        SetWindowTextW(g_hTokoEntIn, Utf8ToWide(it->second.link_in).c_str());
                        SetWindowTextW(g_hTokoEntOut, Utf8ToWide(it->second.link_out).c_str());
                    }
                }
            } else if (id == IDC_TOKO_BTN_SIMPAN) {
                std::string kode = TrimString(GetWindowTextUtf8(g_hTokoEntKode));
                std::transform(kode.begin(), kode.end(), kode.begin(), ::toupper);
                std::string link_in = TrimString(GetWindowTextUtf8(g_hTokoEntIn));
                std::string link_out = TrimString(GetWindowTextUtf8(g_hTokoEntOut));

                if (kode.empty() || link_in.empty() || link_out.empty()) {
                    MessageBoxW(hWnd, L"Kode Toko, Link IN, dan Link OUT wajib diisi semua!",
                                L"Data Belum Lengkap", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                g_databaseForm[kode] = {link_in, link_out};
                std::string err;
                JsonConfig::SaveStoreDatabase(FILE_KONFIG_TOKO, g_databaseForm, err);
                RefreshTokoListbox();
                TulisLog("Konfigurasi toko '" + kode + "' berhasil disimpan ke " + FILE_KONFIG_TOKO + ".", "INFO");
                std::wstring msg = L"Data toko '" + Utf8ToWide(kode) + L"' berhasil disimpan!";
                MessageBoxW(hWnd, msg.c_str(), L"Tersimpan", MB_OK | MB_ICONINFORMATION);

            } else if (id == IDC_TOKO_BTN_HAPUS) {
                int sel = static_cast<int>(SendMessageW(g_hTokoList, LB_GETCURSEL, 0, 0));
                if (sel == LB_ERR) {
                    MessageBoxW(hWnd, L"Klik salah satu toko di daftar atas yang ingin dihapus!",
                                L"Pilih Toko", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (g_databaseForm.size() <= 1) {
                    MessageBoxW(hWnd, L"Minimal harus ada 1 toko tersisa di database!",
                                L"Tidak Bisa Dihapus", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                wchar_t wkode[128] = {0};
                SendMessageW(g_hTokoList, LB_GETTEXT, sel, reinterpret_cast<LPARAM>(wkode));
                std::string kode = WideToUtf8(wkode);

                std::wstring ask = L"Yakin ingin menghapus toko '" + std::wstring(wkode) + L"'?";
                if (MessageBoxW(hWnd, ask.c_str(), L"Konfirmasi Hapus", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    g_databaseForm.erase(kode);
                    std::string err;
                    JsonConfig::SaveStoreDatabase(FILE_KONFIG_TOKO, g_databaseForm, err);
                    RefreshTokoListbox();
                    SetWindowTextW(g_hTokoEntKode, L"");
                    SetWindowTextW(g_hTokoEntIn, L"");
                    SetWindowTextW(g_hTokoEntOut, L"");
                    TulisLog("Toko '" + kode + "' dihapus dari konfigurasi.", "WARN");
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            EnableWindow(g_hMainWnd, TRUE);
            SetForegroundWindow(g_hMainWnd);
            DestroyWindow(hWnd);
            g_hTokoWin = nullptr;
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void BukaJendelaKelolaToko() {
    if (g_hTokoWin) return;

    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = TokoModalWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"BotKeepStock_TokoModal";
        RegisterClassExW(&wc);
        class_registered = true;
    }

    RECT rcParent;
    GetWindowRect(g_hMainWnd, &rcParent);
    int w = 500, h = 405;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    EnableWindow(g_hMainWnd, FALSE);
    g_hTokoWin = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"BotKeepStock_TokoModal",
        L"Kelola Database Toko (config_toko.json)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, g_hMainWnd, nullptr, g_hInst, nullptr);
}

// ============================================================================
// AKSI TOMBOL JENDELA UTAMA
// ============================================================================
static void PilihFileKlik() {
    wchar_t file_buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFilter = L"Excel Files (*.xlsx)\0*.xlsx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Pilih File Excel Keep Stock";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        LoadExcelFileFromPath(file_buf);
    }
}

static void SetControlRunningState(bool running) {
    g_isRunning.store(running);

    EnableWindow(g_btnStart, !running);
    SetWindowTextW(g_btnStart, running ? L"Memproses..." : L"> JALANKAN BOT");

    EnableWindow(g_btnPilihFile, !running);
    EnableWindow(g_btnPasteClip, !running);
    EnableWindow(g_btnCekValidasi, !running);
    EnableWindow(g_btnHapusBaris, !running);
    EnableWindow(g_btnResetTabel, !running);
    EnableWindow(g_btnTambahM, !running);
    EnableWindow(g_btnKelolaToko, !running);
    EnableWindow(g_spinEditMulai, !running);
    EnableWindow(g_spinUpDownMulai, !running);
    EnableWindow(g_spinEditRetry, !running);
    EnableWindow(g_spinUpDownRetry, !running);
    EnableWindow(g_comboWorker, !running);

    EnableWindow(g_btnPause, running);
    SetWindowTextW(g_btnPause, L"|| PAUSE");
    g_buttonStyles[IDC_BTN_PAUSE].bg_normal = RGB(245, 158, 11);

    EnableWindow(g_btnStop, running);

    UpdateRetryFailedButtonState();

    InvalidateRect(g_btnStart, nullptr, TRUE);
    InvalidateRect(g_btnPause, nullptr, TRUE);
    InvalidateRect(g_btnStop, nullptr, TRUE);
}

static void MulaiProsesBot(bool only_failed_rows) {
    CommitInlineEdit(true);

    std::string toko = TrimString(GetWindowTextUtf8(g_comboToko));
    std::string jenis = TrimString(GetWindowTextUtf8(g_comboJenis));
    std::string nama = TrimString(GetWindowTextUtf8(g_entryNama));
    std::string posisi = TrimString(GetWindowTextUtf8(g_comboPosisi));
    bool mode_headless = (SendMessageW(g_chkHeadless, BM_GETCHECK, 0, 0) == BST_CHECKED);

    int num_workers = 1;
    int worker_sel = static_cast<int>(SendMessageW(g_comboWorker, CB_GETCURSEL, 0, 0));
    if (worker_sel >= 0 && worker_sel < 4) {
        num_workers = worker_sel + 1;
    }

    int max_retry = 3;
    try {
        max_retry = std::clamp(std::stoi(TrimString(GetWindowTextUtf8(g_spinEditRetry))), 1, 10);
    } catch (...) {
        max_retry = 3;
    }

    std::vector<XlsxReader::StockRow> df_input = AmbilDataDariTabel();
    if (df_input.empty()) {
        MessageBoxW(g_hMainWnd,
                    L"Tabel masih kosong!\nSilakan klik '📂 Buka Excel', Drag & Drop file .xlsx ke jendela ini, atau klik '📋 Paste (Ctrl+V)'.",
                    L"Data Kosong", MB_OK | MB_ICONWARNING);
        return;
    }

    int total_data = static_cast<int>(df_input.size());

    if (toko.empty() || toko == "Pilih Toko" || g_databaseForm.find(toko) == g_databaseForm.end() ||
        (jenis != "IN" && jenis != "OUT") || nama.empty() || posisi.empty() || posisi == "Pilih Posisi") {
        MessageBoxW(g_hMainWnd,
                    L"Semua kolom pilihan Toko, Jenis Form, dan Identitas PIC wajib diisi!",
                    L"Peringatan", MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<int> target_indices;
    int baris_mulai = 1;

    if (only_failed_rows) {
        for (int idx : g_failedRowIndices) {
            if (idx >= 0 && idx < total_data) {
                target_indices.push_back(idx);
            }
        }
        if (target_indices.empty()) {
            MessageBoxW(g_hMainWnd, L"Tidak ada baris gagal yang perlu diulangi!", L"Info", MB_OK | MB_ICONINFORMATION);
            return;
        }
    } else {
        try {
            baris_mulai = std::stoi(TrimString(GetWindowTextUtf8(g_spinEditMulai)));
            if (baris_mulai < 1 || baris_mulai > total_data) {
                std::wstring msg = L"Mulai dari data ke-" + std::to_wstring(baris_mulai) +
                                   L" tidak valid (Total data: " + std::to_wstring(total_data) + L" baris)!";
                MessageBoxW(g_hMainWnd, msg.c_str(), L"Baris Tidak Valid", MB_OK | MB_ICONWARNING);
                return;
            }
        } catch (...) {
            MessageBoxW(g_hMainWnd, L"Kolom 'Mulai dari data ke' harus berupa angka bulat!",
                        L"Input Baris Salah", MB_OK | MB_ICONWARNING);
            return;
        }

        for (int idx = baris_mulai - 1; idx < total_data; ++idx) {
            target_indices.push_back(idx);
        }
    }

    // Validasi Pra-Jalan: Cek kolom kosong, QTY bukan angka, dan baris duplikat
    std::vector<std::string> baris_bolong;
    std::vector<std::string> baris_qty_invalid;
    std::vector<std::string> baris_duplikat;
    std::map<std::string, int> seen_keys;

    for (int idx : target_indices) {
        const auto& r = df_input[idx];
        std::string k_keep = XlsxReader::BersihkanAngka(r.keep_stock);
        std::string k_sku  = XlsxReader::BersihkanAngka(r.sku);
        std::string k_qty  = XlsxReader::BersihkanAngka(r.sku_qty);
        std::string k_gon  = XlsxReader::BersihkanAngka(r.gondola);

        if (k_keep.empty() || k_sku.empty() || k_qty.empty() || k_gon.empty()) {
            baris_bolong.push_back(std::to_string(idx + 1));
            SetRowStatusAsync(idx, "! Kolom Kosong", false);
        } else if (!IsValidPositiveNumber(k_qty)) {
            baris_qty_invalid.push_back(std::to_string(idx + 1));
            SetRowStatusAsync(idx, "! QTY Invalid", false);
        } else {
            std::string key = MakeRowDuplicateKey(r);
            auto it = seen_keys.find(key);
            if (it != seen_keys.end()) {
                baris_duplikat.push_back(std::to_string(idx + 1));
                SetRowStatusAsync(idx, "! Duplikat (#" + std::to_string(it->second + 1) + ")", false);
            } else {
                seen_keys[key] = idx;
                SetRowStatusAsync(idx, "Menunggu", false);
            }
        }
    }

    auto format_sample = [](const std::vector<std::string>& v) -> std::wstring {
        std::string s;
        size_t lim = std::min<size_t>(5, v.size());
        for (size_t i = 0; i < lim; ++i) {
            if (i > 0) s += ", ";
            s += v[i];
        }
        if (v.size() > 5) s += "...";
        return Utf8ToWide(s);
    };

    if (!baris_bolong.empty() || !baris_qty_invalid.empty() || !baris_duplikat.empty()) {
        std::wostringstream warn_ss;
        warn_ss << L"Deteksi Validasi Data menemukan potensi masalah:\n\n";
        if (!baris_bolong.empty()) {
            warn_ss << L"• Kolom Kosong (" << baris_bolong.size() << L" baris): baris ke-" << format_sample(baris_bolong) << L"\n";
        }
        if (!baris_qty_invalid.empty()) {
            warn_ss << L"• SKU QTY Bukan Angka (" << baris_qty_invalid.size() << L" baris): baris ke-" << format_sample(baris_qty_invalid) << L"\n";
        }
        if (!baris_duplikat.empty()) {
            warn_ss << L"• Baris Duplikat (" << baris_duplikat.size() << L" baris): baris ke-" << format_sample(baris_duplikat) << L"\n";
        }
        warn_ss << L"\nApakah Anda tetap ingin melanjutkan proses bot?";
        if (MessageBoxW(g_hMainWnd, warn_ss.str().c_str(), L"Peringatan Validasi & Duplikat", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    SaveCurrentUserPrefs();

    g_failedRowIndices.clear();
    g_isPaused.store(false);
    g_isStopped.store(false);
    SetControlRunningState(true);

    std::string link_otomatis = (jenis == "IN")
                                ? g_databaseForm[toko].link_in
                                : g_databaseForm[toko].link_out;

    SendMessageW(g_progressBar, PBM_SETPOS, 0, 0);
    g_colStatusLabel = RGB(59, 130, 246);
    SetWindowTextW(g_lblStatus, L"Menyiapkan browser...");
    InvalidateRect(g_lblStatus, nullptr, TRUE);

    std::thread master_worker(ProsesSeleniumMasterThread,
                              std::move(df_input), std::move(target_indices),
                              link_otomatis, nama, posisi, mode_headless,
                              num_workers, max_retry);
    master_worker.detach();
}

static void PauseKlik() {
    if (!g_isRunning.load()) return;
    if (!g_isPaused.load()) {
        g_isPaused.store(true);
        SetWindowTextW(g_btnPause, L"> LANJUTKAN");
        g_buttonStyles[IDC_BTN_PAUSE].bg_normal = RGB(37, 99, 235);
        InvalidateRect(g_btnPause, nullptr, TRUE);

        g_colStatusLabel = RGB(245, 158, 11);
        SetWindowTextW(g_lblStatus, L"Menunggu baris saat ini selesai untuk menjeda...");
        InvalidateRect(g_lblStatus, nullptr, TRUE);
        TulisLog("Perintah Pause diterima. Menunggu baris aktif selesai...", "WARN");
    } else {
        g_isPaused.store(false);
        SetWindowTextW(g_btnPause, L"|| PAUSE");
        g_buttonStyles[IDC_BTN_PAUSE].bg_normal = RGB(245, 158, 11);
        InvalidateRect(g_btnPause, nullptr, TRUE);

        g_colStatusLabel = RGB(59, 130, 246);
        SetWindowTextW(g_lblStatus, L"Melanjutkan proses...");
        InvalidateRect(g_lblStatus, nullptr, TRUE);
        TulisLog("Proses dilanjutkan kembali.", "INFO");
    }
}

static void StopKlik() {
    if (!g_isRunning.load()) return;
    if (MessageBoxW(g_hMainWnd,
                    L"Apakah Anda yakin ingin menghentikan proses bot sepenuhnya?",
                    L"Konfirmasi Stop", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_isStopped.store(true);
        g_isPaused.store(false);
        EnableWindow(g_btnPause, FALSE);
        EnableWindow(g_btnStop, FALSE);
        InvalidateRect(g_btnPause, nullptr, TRUE);
        InvalidateRect(g_btnStop, nullptr, TRUE);

        g_colStatusLabel = RGB(239, 68, 68);
        SetWindowTextW(g_lblStatus, L"Menghentikan bot dan menutup browser...");
        InvalidateRect(g_lblStatus, nullptr, TRUE);
        TulisLog("Menghentikan bot...", "ERROR");
    }
}

// ============================================================================
// WINDOW PROCEDURE UTAMA (MODERN UI v6.1)
// ============================================================================
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            DragAcceptFiles(hWnd, TRUE);

            // Inisialisasi Font Modern
            g_hFontHeaderTitle = CreateFontW(-17, 0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontHeaderSub   = CreateFontW(-11, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontStatLbl     = CreateFontW(-9,  0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontStatNum     = CreateFontW(-14, 0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontCardTitle   = CreateFontW(-12, 0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontBold9       = CreateFontW(-12, 0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontReg9        = CreateFontW(-12, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontItalic9     = CreateFontW(-12, 0, 0, 0, FW_NORMAL,   TRUE,  FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontBold8       = CreateFontW(-11, 0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontReg8        = CreateFontW(-11, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontItalic8     = CreateFontW(-11, 0, 0, 0, FW_NORMAL,   TRUE,  FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontTips7       = CreateFontW(-10, 0, 0, 0, FW_NORMAL,   TRUE,  FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_hFontLog         = CreateFontW(-11, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");

            // Tombol Toggle Tema di Kanan Atas Banner
            g_btnTheme = CreateWindowW(L"BUTTON", L"🌙 Dark",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(IDC_BTN_THEME), g_hInst, nullptr);

            // --- KARTU 1: SUMBER DATA & TABEL INTERAKTIF TERPADU ---
            g_btnPilihFile = CreateWindowW(L"BUTTON", L"📂 Buka Excel",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(IDC_BTN_PILIH_FILE), g_hInst, nullptr);
            g_btnPasteClip = CreateWindowW(L"BUTTON", L"📋 Paste (Ctrl+V)",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(IDC_BTN_PASTE_CLIP), g_hInst, nullptr);
            g_btnCekValidasi = CreateWindowW(L"BUTTON", L"✔ Cek Duplikat",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(IDC_BTN_CEK_VALIDASI), g_hInst, nullptr);
            g_btnHapusBaris = CreateWindowW(L"BUTTON", L"✕ Hapus (Del)",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(IDC_BTN_HAPUS_BARIS), g_hInst, nullptr);
            g_btnResetTabel = CreateWindowW(L"BUTTON", L"Reset",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(IDC_BTN_RESET_TABEL), g_hInst, nullptr);
            g_lblFileTerpilih = CreateWindowW(L"STATIC", L"Siap: Buka/Drop file .xlsx atau Paste Excel",
                WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);

            g_lblKeep = CreateWindowW(L"STATIC", L"Keep:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_entKeep = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_ENT_M_KEEP), g_hInst, nullptr);
            g_lblSku  = CreateWindowW(L"STATIC", L"SKU:",  WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_entSku  = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_ENT_M_SKU), g_hInst, nullptr);
            g_lblQty  = CreateWindowW(L"STATIC", L"QTY:",  WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_entQty  = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_ENT_M_QTY), g_hInst, nullptr);
            g_lblGondola = CreateWindowW(L"STATIC", L"Gondola:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_entGondola = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_ENT_M_GONDOLA), g_hInst, nullptr);
            g_btnTambahM = CreateWindowW(L"BUTTON", L"+ Tambah", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_TAMBAH_M), g_hInst, nullptr);
            g_lblInfoTabel = CreateWindowW(L"STATIC", L"Total: 0 baris",
                WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);

            g_origGondolaEditProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_entGondola, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GondolaEditSubclassProc)));

            g_treeData = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_TREE_DATA), g_hInst, nullptr);
            ListView_SetExtendedListViewStyle(g_treeData, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            g_origListViewProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_treeData, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListViewSubclassProc)));

            const wchar_t* col_names[6] = {L"No", L"Alamat Keep Stock", L"SKU", L"SKU QTY", L"Alamat Gondola", L"Status"};
            int col_widths[6] = {42, 135, 130, 72, 135, 140};
            for (int c = 0; c < 6; ++c) {
                LVCOLUMNW lvc = {0};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
                lvc.fmt = LVCFMT_CENTER;
                lvc.cx = col_widths[c];
                lvc.pszText = const_cast<LPWSTR>(col_names[c]);
                ListView_InsertColumn(g_treeData, c, &lvc);
            }

            g_lblTips = CreateWindowW(L"STATIC",
                L"Tips: Anda bisa Drag & Drop file .xlsx langsung ke jendela ini, atau Copy-Paste dari Excel. Double-click sel untuk edit.",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);

            // --- KARTU 2: KONFIGURASI ---
            g_lblPilihToko = CreateWindowW(L"STATIC", L"Pilih Toko :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_comboToko = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_COMBO_TOKO), g_hInst, nullptr);
            g_btnKelolaToko = CreateWindowW(L"BUTTON", L"Kelola Toko", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_KELOLA_TOKO), g_hInst, nullptr);

            g_lblJenisForm = CreateWindowW(L"STATIC", L"Jenis Form :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_comboJenis = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_COMBO_JENIS), g_hInst, nullptr);
            SendMessageW(g_comboJenis, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"IN"));
            SendMessageW(g_comboJenis, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"OUT"));

            g_lblMulaiData = CreateWindowW(L"STATIC", L"Mulai data ke :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_spinEditMulai = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
                WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_SPIN_EDIT_MULAI), g_hInst, nullptr);
            g_spinUpDownMulai = CreateWindowW(UPDOWN_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_SPIN_UPDOWN_MULAI), g_hInst, nullptr);
            SendMessageW(g_spinUpDownMulai, UDM_SETBUDDY, reinterpret_cast<WPARAM>(g_spinEditMulai), 0);
            SendMessageW(g_spinUpDownMulai, UDM_SETRANGE32, 1, 99999);
            SendMessageW(g_spinUpDownMulai, UDM_SETPOS32, 0, 1);

            g_lblMaxRetry = CreateWindowW(L"STATIC", L"Maks Retry :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_spinEditRetry = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"3",
                WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_SPIN_EDIT_RETRY), g_hInst, nullptr);
            g_spinUpDownRetry = CreateWindowW(UPDOWN_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_SPIN_UPDOWN_RETRY), g_hInst, nullptr);
            SendMessageW(g_spinUpDownRetry, UDM_SETBUDDY, reinterpret_cast<WPARAM>(g_spinEditRetry), 0);
            SendMessageW(g_spinUpDownRetry, UDM_SETRANGE32, 1, 10);
            SendMessageW(g_spinUpDownRetry, UDM_SETPOS32, 0, 3);

            g_lblNamaPic = CreateWindowW(L"STATIC", L"Nama PIC :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_entryNama = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_ENTRY_NAMA), g_hInst, nullptr);

            g_lblPosisi = CreateWindowW(L"STATIC", L"Posisi :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_comboPosisi = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_COMBO_POSISI), g_hInst, nullptr);
            const wchar_t* posisi_list[] = {L"RM", L"AM", L"DM", L"BM", L"ABM", L"MP", L"AMP"};
            for (const wchar_t* p : posisi_list) {
                SendMessageW(g_comboPosisi, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p));
            }

            g_lblWorker = CreateWindowW(L"STATIC", L"Kecepatan :", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_comboWorker = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_COMBO_WORKER), g_hInst, nullptr);
            const wchar_t* worker_list[] = {
                L"1 Browser (Normal)",
                L"2 Browser (Cepat 2x)",
                L"3 Browser (Turbo 3x)",
                L"4 Browser (Ekstrem 4x)"
            };
            for (const wchar_t* w_opt : worker_list) {
                SendMessageW(g_comboWorker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w_opt));
            }
            SendMessageW(g_comboWorker, CB_SETCURSEL, 0, 0);

            g_chkHeadless = CreateWindowW(L"BUTTON", L"Headless",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_CHK_HEADLESS), g_hInst, nullptr);
            SendMessageW(g_chkHeadless, BM_SETCHECK, BST_CHECKED, 0);

            // --- KARTU 3: STATUS & PROGRESS BAR ---
            g_lblStatus = CreateWindowW(L"STATIC", L"Bot siap dijalankan.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_lblEta = CreateWindowW(L"STATIC", L"Estimasi sisa waktu: -", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hWnd, nullptr, g_hInst, nullptr);
            g_progressBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_PROGRESS_BAR), g_hInst, nullptr);

            // --- TOMBOL KONTROL UTAMA ---
            g_btnStart = CreateWindowW(L"BUTTON", L"> JALANKAN BOT", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_START), g_hInst, nullptr);
            g_btnPause = CreateWindowW(L"BUTTON", L"|| PAUSE", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_PAUSE), g_hInst, nullptr);
            g_btnStop  = CreateWindowW(L"BUTTON", L"[] STOP", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_STOP), g_hInst, nullptr);
            g_btnRetryFailed = CreateWindowW(L"BUTTON", L"↻ ULANGI GAGAL (0)", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_RETRY_FAILED), g_hInst, nullptr);

            // --- KARTU 4: LIVE LOG AKTIVITAS ---
            g_btnClearLog = CreateWindowW(L"BUTTON", L"Bersihkan Log", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_BTN_CLEAR_LOG), g_hInst, nullptr);

            LoadLibraryW(L"Msftedit.dll");
            g_richLog = CreateWindowExW(
                WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(IDC_RICH_LOG), g_hInst, nullptr);

            // Pasang efek hover & rounded style ke seluruh tombol
            for (HWND hBtn : {g_btnTheme, g_btnPilihFile, g_btnPasteClip, g_btnCekValidasi,
                              g_btnHapusBaris, g_btnResetTabel, g_btnTambahM, g_btnKelolaToko,
                              g_btnStart, g_btnPause, g_btnStop, g_btnRetryFailed, g_btnClearLog}) {
                SubclassModernButton(hBtn);
            }

            // Terapkan Font
            for (HWND h : {g_lblMulaiData, g_spinEditMulai, g_lblMaxRetry, g_spinEditRetry,
                           g_btnStart, g_btnPause, g_btnStop, g_btnRetryFailed}) {
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold9), TRUE);
            }
            for (HWND h : {g_treeData, g_lblPilihToko, g_comboToko, g_lblJenisForm, g_comboJenis,
                           g_lblNamaPic, g_entryNama, g_lblPosisi, g_comboPosisi, g_lblWorker, g_comboWorker}) {
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontReg9), TRUE);
            }
            for (HWND h : {g_btnTheme, g_btnPilihFile, g_btnPasteClip, g_btnCekValidasi,
                           g_btnHapusBaris, g_btnResetTabel, g_lblInfoTabel,
                           g_btnTambahM, g_btnKelolaToko, g_btnClearLog, g_lblEta}) {
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold8), TRUE);
            }
            for (HWND h : {g_lblKeep, g_entKeep, g_lblSku, g_entSku, g_lblQty, g_entQty,
                           g_lblGondola, g_entGondola, g_chkHeadless}) {
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontReg8), TRUE);
            }
            SendMessageW(g_lblFileTerpilih, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontItalic8), TRUE);
            SendMessageW(g_lblStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontItalic8), TRUE);
            SendMessageW(g_lblTips, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontTips7), TRUE);
            SendMessageW(g_richLog, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontLog), TRUE);

            // Muat Database Toko & Preferensi Terakhir Pengguna
            bool used_def = false;
            std::string warn_msg;
            g_databaseForm = JsonConfig::LoadStoreDatabase(FILE_KONFIG_TOKO, used_def, warn_msg);
            RefreshMainComboToko();

            JsonConfig::UserPrefs prefs = JsonConfig::LoadUserPrefs(FILE_PREFERENSI);
            if (!prefs.toko.empty() && g_databaseForm.count(prefs.toko)) {
                SendMessageW(g_comboToko, CB_SELECTSTRING, static_cast<WPARAM>(-1),
                             reinterpret_cast<LPARAM>(Utf8ToWide(prefs.toko).c_str()));
            }
            if (prefs.jenis == "IN" || prefs.jenis == "OUT") {
                SendMessageW(g_comboJenis, CB_SELECTSTRING, static_cast<WPARAM>(-1),
                             reinterpret_cast<LPARAM>(Utf8ToWide(prefs.jenis).c_str()));
            }
            if (!prefs.nama.empty()) {
                SetWindowTextW(g_entryNama, Utf8ToWide(prefs.nama).c_str());
            }
            if (!prefs.posisi.empty()) {
                SendMessageW(g_comboPosisi, CB_SELECTSTRING, static_cast<WPARAM>(-1),
                             reinterpret_cast<LPARAM>(Utf8ToWide(prefs.posisi).c_str()));
            }
            SendMessageW(g_comboWorker, CB_SETCURSEL, std::clamp(prefs.jumlah_worker, 1, 4) - 1, 0);
            SendMessageW(g_spinUpDownRetry, UDM_SETPOS32, 0, std::clamp(prefs.max_retry, 1, 10));
            SendMessageW(g_chkHeadless, BM_SETCHECK, prefs.headless ? BST_CHECKED : BST_UNCHECKED, 0);

            g_isDarkMode = prefs.dark_mode;
            ApplyTheme();

            TulisLog("Sistem v6.1 (Modern UI) siap: Tabel Terpadu, Drag & Drop .xlsx, Dark Mode & Multi-Worker.", "INFO");
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            wchar_t dropped_file[MAX_PATH] = {0};
            if (DragQueryFileW(hDrop, 0, dropped_file, MAX_PATH) > 0) {
                LoadExcelFileFromPath(dropped_file);
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 720;
            mmi->ptMinTrackSize.y = 640;
            return 0;
        }

        case WM_SIZE: {
            UpdateLayout(LOWORD(lParam), HIWORD(lParam));
            return 0;
        }

        case WM_ERASEBKGND: {
            return 1; // Hindari kedipan (flicker), digambar penuh di WM_PAINT
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            PaintMainDashboard(hdc, rc.right, rc.bottom);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            DrawColoredButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hCtl = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            if (hCtl == g_lblFileTerpilih) {
                SetTextColor(hdc, g_colFileLabel);
            } else if (hCtl == g_lblStatus) {
                SetTextColor(hdc, g_colStatusLabel);
            } else if (hCtl == g_lblMulaiData || hCtl == g_lblMaxRetry) {
                SetTextColor(hdc, ThemeCardTitle());
            } else if (hCtl == g_lblInfoTabel) {
                SetTextColor(hdc, g_isDarkMode ? RGB(74, 222, 128) : RGB(22, 163, 74));
            } else if (hCtl == g_lblEta || hCtl == g_lblTips) {
                SetTextColor(hdc, ThemeTextMuted());
            } else {
                SetTextColor(hdc, ThemeTextPrimary());
            }
            return reinterpret_cast<LRESULT>(g_hBrushCardBg);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, ThemeInputBg());
            SetTextColor(hdc, ThemeTextPrimary());
            return reinterpret_cast<LRESULT>(g_hBrushInputBg);
        }

        case WM_NOTIFY: {
            auto* nmhdr = reinterpret_cast<LPNMHDR>(lParam);
            if (nmhdr->idFrom == IDC_TREE_DATA) {
                if (nmhdr->code == NM_DBLCLK) {
                    auto* nmitem = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
                    if (nmitem->iItem >= 0 && nmitem->iSubItem >= 1 && nmitem->iSubItem <= 4) {
                        MulaiEditSelTabel(nmitem->iItem, nmitem->iSubItem);
                    }
                } else if (nmhdr->code == NM_CUSTOMDRAW) {
                    auto* lvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                    if (lvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (lvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        int row = static_cast<int>(lvcd->nmcd.dwItemSpec);
                        wchar_t st_buf[128] = {0};
                        ListView_GetItemText(g_treeData, row, 5, st_buf, 128);
                        std::wstring st(st_buf);

                        // Zebra striping default (selang-seling warna baris)
                        lvcd->clrTextBk = (row % 2 == 0) ? ThemeTableBg() : ThemeTableAltBg();
                        lvcd->clrText   = ThemeTextPrimary();

                        if (st.find(L"Sukses") != std::wstring::npos) {
                            lvcd->clrTextBk = g_isDarkMode ? RGB(6, 78, 59)    : RGB(220, 252, 231);
                            lvcd->clrText   = g_isDarkMode ? RGB(167, 243, 208): RGB(22, 101, 52);
                        } else if (st.find(L"Memproses") != std::wstring::npos) {
                            lvcd->clrTextBk = g_isDarkMode ? RGB(30, 58, 138)  : RGB(219, 234, 254);
                            lvcd->clrText   = g_isDarkMode ? RGB(191, 219, 254): RGB(30, 64, 175);
                        } else if (st.find(L"Retry") != std::wstring::npos || (!st.empty() && st[0] == L'!')) {
                            lvcd->clrTextBk = g_isDarkMode ? RGB(120, 53, 15)  : RGB(254, 243, 199);
                            lvcd->clrText   = g_isDarkMode ? RGB(253, 230, 138): RGB(180, 83, 9);
                        } else if (st.find(L"Gagal") != std::wstring::npos) {
                            lvcd->clrTextBk = g_isDarkMode ? RGB(127, 29, 29)  : RGB(254, 226, 226);
                            lvcd->clrText   = g_isDarkMode ? RGB(254, 202, 202): RGB(153, 27, 27);
                        }
                        return CDRF_NEWFONT;
                    }
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_BTN_THEME:
                    g_isDarkMode = !g_isDarkMode;
                    ApplyTheme();
                    SaveCurrentUserPrefs();
                    break;
                case IDC_BTN_PILIH_FILE:
                    PilihFileKlik();
                    break;
                case IDC_BTN_PASTE_CLIP:
                    PasteDariClipboard();
                    break;
                case IDC_BTN_CEK_VALIDASI:
                    CekDuplikatDanValidasiKlik();
                    break;
                case IDC_BTN_HAPUS_BARIS:
                    HapusBarisTerpilih();
                    break;
                case IDC_BTN_RESET_TABEL:
                    KosongkanTabel();
                    break;
                case IDC_BTN_TAMBAH_M:
                    TambahBarisManual();
                    break;
                case IDC_BTN_KELOLA_TOKO:
                    BukaJendelaKelolaToko();
                    break;
                case IDC_BTN_START:
                    MulaiProsesBot(false);
                    break;
                case IDC_BTN_RETRY_FAILED:
                    MulaiProsesBot(true);
                    break;
                case IDC_BTN_PAUSE:
                    PauseKlik();
                    break;
                case IDC_BTN_STOP:
                    StopKlik();
                    break;
                case IDC_BTN_CLEAR_LOG:
                    SetWindowTextW(g_richLog, L"");
                    break;
            }
            return 0;
        }

        // --- HANDLER PESAN ASYNC DARI WORKER THREAD ---
        case WM_APP_LOG: {
            auto* payload = reinterpret_cast<LogPayload*>(lParam);
            if (payload) {
                AppendRichLogOnMainThread(payload->text, payload->color);
                delete payload;
            }
            return 0;
        }

        case WM_APP_STATUS: {
            auto* payload = reinterpret_cast<StatusPayload*>(lParam);
            if (payload) {
                g_colStatusLabel = payload->color;
                SetWindowTextW(g_lblStatus, payload->text.c_str());
                InvalidateRect(g_lblStatus, nullptr, TRUE);
                delete payload;
            }
            return 0;
        }

        case WM_APP_ETA: {
            auto* wstr = reinterpret_cast<std::wstring*>(lParam);
            if (wstr) {
                SetWindowTextW(g_lblEta, wstr->c_str());
                delete wstr;
            }
            return 0;
        }

        case WM_APP_PROGRESS: {
            int pos_x2 = static_cast<int>(wParam);
            int max_x2 = static_cast<int>(lParam);
            SendMessageW(g_progressBar, PBM_SETRANGE32, 0, max_x2);
            SendMessageW(g_progressBar, PBM_SETPOS, pos_x2, 0);
            return 0;
        }

        case WM_APP_ROW_STATUS: {
            auto* payload = reinterpret_cast<RowStatusPayload*>(lParam);
            if (payload) {
                int count = ListView_GetItemCount(g_treeData);
                if (payload->row_index >= 0 && payload->row_index < count) {
                    ListView_SetItemText(g_treeData, payload->row_index, 5,
                                         const_cast<LPWSTR>(payload->status_text.c_str()));
                    ListView_RedrawItems(g_treeData, payload->row_index, payload->row_index);
                    if (payload->auto_scroll) {
                        ListView_EnsureVisible(g_treeData, payload->row_index, FALSE);
                    }
                    RecalculateDashboardStats();
                }
                delete payload;
            }
            return 0;
        }

        case WM_APP_FINISHED: {
            auto* fin = reinterpret_cast<FinishedPayload*>(lParam);
            if (fin) {
                g_failedRowIndices = fin->failed_rows;
            }
            SetControlRunningState(false);
            RecalculateDashboardStats();

            if (fin) {
                if (fin->had_system_error) {
                    g_colStatusLabel = RGB(239, 68, 68);
                    SetWindowTextW(g_lblStatus, L"Terjadi kesalahan sistem.");
                    InvalidateRect(g_lblStatus, nullptr, TRUE);

                    TulisLog("X ERROR SISTEM: " + WideToUtf8(fin->error_msg), "ERROR");
                    std::wstring msg = L"Terjadi kesalahan saat menjalankan browser:\n" + fin->error_msg;
                    MessageBoxW(hWnd, msg.c_str(), L"Error Sistem", MB_OK | MB_ICONERROR);

                } else if (fin->was_stopped) {
                    int baris_lanjut = fin->last_success_row + 1;
                    g_colStatusLabel = RGB(239, 68, 68);
                    std::wstring st = L"Dihentikan. Sukses sesi ini: " +
                                      std::to_wstring(fin->success_count_session) + L" baris | Gagal: " +
                                      std::to_wstring(fin->failed_rows.size()) + L" baris.";
                    SetWindowTextW(g_lblStatus, st.c_str());
                    InvalidateRect(g_lblStatus, nullptr, TRUE);

                    TulisLog("Bot dihentikan pengguna. Untuk lanjut, mulai dari baris ke-" +
                             std::to_string(baris_lanjut) + ".", "WARN");

                    if (baris_lanjut >= 1 && baris_lanjut <= fin->total_data) {
                        SendMessageW(g_spinUpDownMulai, UDM_SETPOS32, 0, baris_lanjut);
                    }

                    std::wstring info =
                        L"Bot telah dihentikan.\n\n"
                        L"• Berhasil diinput sesi ini: " + std::to_wstring(fin->success_count_session) + L" baris\n"
                        L"• Baris gagal (dilewati): " + std::to_wstring(fin->failed_rows.size()) + L" baris\n"
                        L"• Baris terakhir sukses: Baris ke-" + std::to_wstring(fin->last_success_row) + L"\n\n"
                        L"Kolom 'Mulai dari data ke' telah otomatis diatur ke angka " + std::to_wstring(baris_lanjut) + L".";
                    MessageBoxW(hWnd, info.c_str(), L"Proses Dihentikan", MB_OK | MB_ICONINFORMATION);

                } else if (!fin->failed_rows.empty()) {
                    g_colStatusLabel = RGB(245, 158, 11);
                    std::wstring st = L"Selesai sebagian: " + std::to_wstring(fin->success_count_session) +
                                      L" sukses, " + std::to_wstring(fin->failed_rows.size()) +
                                      L" gagal. Klik 'ULANGI GAGAL' untuk coba lagi.";
                    SetWindowTextW(g_lblStatus, st.c_str());
                    InvalidateRect(g_lblStatus, nullptr, TRUE);

                    TulisLog("SELESAI DENGAN CATATAN: " + std::to_string(fin->success_count_session) +
                             " sukses, " + std::to_string(fin->failed_rows.size()) +
                             " baris gagal. Klik tombol 'ULANGI GAGAL' untuk memproses ulang baris yang gagal.", "WARN");

                    std::wstring msg =
                        L"Proses selesai dengan beberapa baris gagal:\n\n"
                        L"• Berhasil diinput: " + std::to_wstring(fin->success_count_session) + L" baris\n"
                        L"• Gagal setelah batas retry: " + std::to_wstring(fin->failed_rows.size()) + L" baris\n\n"
                        L"Anda cukup menekan tombol ungu '↻ ULANGI GAGAL (" +
                        std::to_wstring(fin->failed_rows.size()) + L")' untuk memproses ulang khusus baris yang gagal saja!";
                    MessageBoxW(hWnd, msg.c_str(), L"Selesai (Ada Baris Gagal)", MB_OK | MB_ICONWARNING);

                } else {
                    g_colStatusLabel = RGB(34, 197, 94);
                    SetWindowTextW(g_lblStatus, L"Selesai! Semua data berhasil diinput tanpa terlewat.");
                    InvalidateRect(g_lblStatus, nullptr, TRUE);

                    TulisLog("SELESAI! Total " + std::to_string(fin->success_count_session) +
                             " baris data berhasil diinput.", "SUKSES");
                    SendMessageW(g_spinUpDownMulai, UDM_SETPOS32, 0, 1);
                    MessageBoxW(hWnd, L"Semua data berhasil diinput ke Google Form!",
                                L"Berhasil", MB_OK | MB_ICONINFORMATION);
                }
                delete fin;
            }
            return 0;
        }

        case WM_CLOSE: {
            if (g_isRunning.load()) {
                if (MessageBoxW(hWnd, L"Bot masih berjalan! Yakin ingin menutup aplikasi?",
                                L"Konfirmasi Keluar", MB_YESNO | MB_ICONWARNING) != IDYES) {
                    return 0;
                }
                g_isStopped.store(true);
            }
            SaveCurrentUserPrefs();
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// ENTRY POINT WINMAIN
// ============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;
    RebuildThemeBrushes();

    INITCOMMONCONTROLSEX icex = {sizeof(icex)};
    icex.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"BotKeepStock_MainWndClass";
    RegisterClassExW(&wc);

    int win_w = 740;
    int win_h = 680;
    int scr_w = GetSystemMetrics(SM_CXSCREEN);
    int scr_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = std::max(0, (scr_w - win_w) / 2);
    int pos_y = std::max(5, (scr_h - win_h) / 2);

    HWND hWnd = CreateWindowExW(
        0, L"BotKeepStock_MainWndClass",
        L"Bot Keep Stock Otomatis v6.1 (C++ Native - Modern UI, Multi-Worker & Smart Table)",
        WS_OVERLAPPEDWINDOW,
        pos_x, pos_y, win_w, win_h,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// Fallback main() jika dikompilasi dengan MinGW tanpa flag -municode
#if !defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
#endif
