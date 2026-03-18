#pragma GCC optimize("O2")
#include "gui.h"
#include "zip.h"
#include "../pipeline/convert.h"
#include "../cli/args.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---- globals ----
static HWND g_hw;
static std::vector<std::string> g_files;
static std::vector<std::string> g_outs;
static std::atomic<bool> g_busy{false};
static std::atomic<bool> g_cancel{false};

// ---- helpers ----
static std::vector<std::string> open_mp4s(HWND hw) {
    static char buf[32768];
    memset(buf, 0, sizeof buf);

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner   = hw;
    ofn.lpstrFilter = "MP4 Files\0*.mp4\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof buf;
    ofn.Flags       = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST;

    std::vector<std::string> out;
    if (!GetOpenFileNameA(&ofn)) return out;

    // Parse null-separated results
    const char* p = buf;
    std::string dir = p;
    p += dir.size() + 1;

    if (*p == '\0') {
        // Single file selected -- buf contains full path directly
        out.push_back(dir);
    } else {
        // Multiple files: first token is directory, rest are filenames
        while (*p) {
            std::string fn = p;
            out.push_back(dir + "\\" + fn);
            p += fn.size() + 1;
        }
    }
    return out;
}

static std::string derive_output(const std::string& s) {
    auto dot = s.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = s.substr(dot);
        // case-insensitive extension check
        std::string lo = ext;
        for (auto& c : lo) c = (char)tolower((unsigned char)c);
        if (lo == ".mp4" || lo == ".avi" || lo == ".mkv" ||
            lo == ".mov" || lo == ".webm")
            return s.substr(0, dot) + ".gif";
    }
    return s + ".gif";
}

static std::string basename(const std::string& path) {
    auto p1 = path.rfind('\\');
    auto p2 = path.rfind('/');
    size_t p = std::string::npos;
    if (p1 != std::string::npos && p2 != std::string::npos) p = std::max(p1, p2);
    else if (p1 != std::string::npos) p = p1;
    else p = p2;
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

static int read_fps(HWND hw) {
    char buf[32];
    GetDlgItemTextA(hw, ID_EDT_FPS, buf, sizeof buf);
    int v = atoi(buf);
    if (v < 1 || v > 60) {
        MessageBoxA(hw, "FPS must be 1-60", "Invalid Input", MB_ICONWARNING);
        return -1;
    }
    return v;
}

static double read_resize(HWND hw) {
    char buf[32];
    GetDlgItemTextA(hw, ID_EDT_RESIZE, buf, sizeof buf);
    double v = atof(buf);
    if (v <= 0.0 || v > 4.0) {
        MessageBoxA(hw, "Resize must be 0.01-4.0", "Invalid Input", MB_ICONWARNING);
        return -1.0;
    }
    return v;
}

static DitherMode read_dither(HWND hw) {
    int i = (int)SendDlgItemMessageA(hw, ID_CMB_DITHER, CB_GETCURSEL, 0, 0);
    switch (i) {
    case 0:  return DitherMode::None;
    case 2:  return DitherMode::Bayer;
    default: return DitherMode::FloydSteinberg;
    }
}

static QuantMode read_quant(HWND hw) {
    int i = (int)SendDlgItemMessageA(hw, ID_CMB_QUANT, CB_GETCURSEL, 0, 0);
    return (i == 1) ? QuantMode::MedianCut : QuantMode::Wu;
}

// ---- worker ----
static void worker_fn(int fps, double resize, DitherMode dm, QuantMode qm) {
    int n = (int)g_files.size();
    for (int i = 0; i < n; ++i) {
        if (g_cancel.load()) break;

        Cfg cfg{};
        cfg.input  = g_files[i].c_str();
        snprintf(cfg.output, sizeof cfg.output, "%s", g_outs[i].c_str());
        cfg.fps    = fps;
        cfg.resize = resize;
        cfg.dither = dm;
        cfg.quant  = qm;

        auto prog = [i](int done, int total) {
            int pct = (total > 0) ? std::clamp(100 * done / total, 0, 100) : 0;
            PostMessage(g_hw, WM_CONV_PROGRESS, (WPARAM)i, (LPARAM)pct);
        };

        try {
            convert(cfg, prog);
            PostMessage(g_hw, WM_CONV_FILE_DONE, (WPARAM)i, 0);
        } catch (const std::exception& e) {
            char* s = _strdup(e.what());
            PostMessage(g_hw, WM_CONV_ERROR, (WPARAM)i, (LPARAM)s);
        }
    }
    PostMessage(g_hw, WM_CONV_ALL_DONE, 0, 0);
    g_busy = false;
}

// ---- WndProc ----
LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {

    case WM_CREATE: {
        HINSTANCE hI = ((LPCREATESTRUCT)lP)->hInstance;

        // Row 1: Open + status
        CreateWindowA("BUTTON", "Open Files", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       10, 10, 100, 28, hw, (HMENU)ID_BTN_OPEN, hI, nullptr);
        CreateWindowA("STATIC", "Idle", WS_CHILD | WS_VISIBLE | SS_LEFT,
                       120, 14, 380, 20, hw, (HMENU)ID_LBL_STATUS, hI, nullptr);

        // Row 2: FPS + Resize
        CreateWindowA("STATIC", "FPS:", WS_CHILD | WS_VISIBLE, 10, 48, 40, 20, hw, nullptr, hI, nullptr);
        CreateWindowA("EDIT", "10", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                       55, 44, 50, 22, hw, (HMENU)ID_EDT_FPS, hI, nullptr);
        CreateWindowA("STATIC", "Resize:", WS_CHILD | WS_VISIBLE, 120, 48, 50, 20, hw, nullptr, hI, nullptr);
        CreateWindowA("EDIT", "1.0", WS_CHILD | WS_VISIBLE | WS_BORDER,
                       175, 44, 50, 22, hw, (HMENU)ID_EDT_RESIZE, hI, nullptr);

        // Row 3: Dither + Quant combos
        CreateWindowA("STATIC", "Dither:", WS_CHILD | WS_VISIBLE, 10, 78, 50, 20, hw, nullptr, hI, nullptr);
        HWND hDith = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                       65, 74, 120, 200, hw, (HMENU)ID_CMB_DITHER, hI, nullptr);
        CreateWindowA("STATIC", "Quant:", WS_CHILD | WS_VISIBLE, 200, 78, 45, 20, hw, nullptr, hI, nullptr);
        HWND hQnt = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                       250, 74, 120, 200, hw, (HMENU)ID_CMB_QUANT, hI, nullptr);

        // Populate combos
        SendMessageA(hDith, CB_ADDSTRING, 0, (LPARAM)"None");
        SendMessageA(hDith, CB_ADDSTRING, 0, (LPARAM)"Floyd-Steinberg");
        SendMessageA(hDith, CB_ADDSTRING, 0, (LPARAM)"Bayer");
        SendMessageA(hDith, CB_SETCURSEL, 1, 0); // default FS

        SendMessageA(hQnt, CB_ADDSTRING, 0, (LPARAM)"Wu");
        SendMessageA(hQnt, CB_ADDSTRING, 0, (LPARAM)"Median Cut");
        SendMessageA(hQnt, CB_SETCURSEL, 0, 0); // default Wu

        // Listbox
        CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER |
                       LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_EXTENDEDSEL,
                       10, 110, 490, 180, hw, (HMENU)ID_LST_FILES, hI, nullptr);

        // Progress bars
        CreateWindowA("STATIC", "File:", WS_CHILD | WS_VISIBLE, 10, 298, 30, 16, hw, nullptr, hI, nullptr);
        HWND hPF = CreateWindowA(PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE,
                       45, 296, 455, 18, hw, (HMENU)ID_PRG_FILE, hI, nullptr);
        CreateWindowA("STATIC", "Total:", WS_CHILD | WS_VISIBLE, 10, 322, 35, 16, hw, nullptr, hI, nullptr);
        HWND hPT = CreateWindowA(PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE,
                       45, 320, 455, 18, hw, (HMENU)ID_PRG_TOTAL, hI, nullptr);

        SendMessageA(hPF, PBM_SETRANGE32, 0, 100);
        SendMessageA(hPF, PBM_SETPOS, 0, 0);
        SendMessageA(hPT, PBM_SETRANGE32, 0, 100);
        SendMessageA(hPT, PBM_SETPOS, 0, 0);

        // Bottom row: Convert, Save As, Export ZIP
        CreateWindowA("BUTTON", "Convert", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       10, 348, 80, 30, hw, (HMENU)ID_BTN_CONV, hI, nullptr);
        CreateWindowA("BUTTON", "Save As", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       100, 348, 80, 30, hw, (HMENU)ID_BTN_SAVEAS, hI, nullptr);
        CreateWindowA("BUTTON", "Export ZIP", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       190, 348, 90, 30, hw, (HMENU)ID_BTN_ZIP, hI, nullptr);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wP)) {

        case ID_BTN_OPEN: {
            if (g_busy) {
                MessageBoxA(hw, "Conversion in progress", "Busy", MB_ICONINFORMATION);
                return 0;
            }
            auto files = open_mp4s(hw);
            if (files.empty()) return 0;

            g_files = std::move(files);
            g_outs.clear();
            for (auto& f : g_files) g_outs.push_back(derive_output(f));

            // Populate listbox
            HWND hLst = GetDlgItem(hw, ID_LST_FILES);
            SendMessageA(hLst, LB_RESETCONTENT, 0, 0);
            for (auto& f : g_files)
                SendMessageA(hLst, LB_ADDSTRING, 0, (LPARAM)basename(f).c_str());

            // Reset progress
            SendDlgItemMessageA(hw, ID_PRG_FILE, PBM_SETPOS, 0, 0);
            SendDlgItemMessageA(hw, ID_PRG_TOTAL, PBM_SETPOS, 0, 0);

            char st[64];
            snprintf(st, sizeof st, "Idle - %d files selected", (int)g_files.size());
            SetDlgItemTextA(hw, ID_LBL_STATUS, st);
            return 0;
        }

        case ID_BTN_CONV: {
            if (g_busy) return 0;
            if (g_files.empty()) {
                MessageBoxA(hw, "No files selected", "Error", MB_ICONWARNING);
                return 0;
            }

            int fps = read_fps(hw);
            if (fps < 0) return 0;
            double resize = read_resize(hw);
            if (resize < 0) return 0;
            DitherMode dm = read_dither(hw);
            QuantMode  qm = read_quant(hw);

            g_busy   = true;
            g_cancel = false;

            SendDlgItemMessageA(hw, ID_PRG_FILE, PBM_SETPOS, 0, 0);
            SendDlgItemMessageA(hw, ID_PRG_TOTAL, PBM_SETPOS, 0, 0);

            std::thread(worker_fn, fps, resize, dm, qm).detach();

            char st[64];
            snprintf(st, sizeof st, "Converting 1/%d...", (int)g_files.size());
            SetDlgItemTextA(hw, ID_LBL_STATUS, st);

            EnableWindow(GetDlgItem(hw, ID_BTN_CONV), FALSE);
            EnableWindow(GetDlgItem(hw, ID_BTN_OPEN), FALSE);
            return 0;
        }

        case ID_BTN_SAVEAS: {
            if (g_busy) {
                MessageBoxA(hw, "Wait for conversion to finish", "Busy", MB_ICONINFORMATION);
                return 0;
            }
            int sel = (int)SendMessageA(GetDlgItem(hw, ID_LST_FILES), LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR || sel < 0 || sel >= (int)g_outs.size()) {
                MessageBoxA(hw, "Select a converted file first", "Save As", MB_ICONWARNING);
                return 0;
            }
            {
                FILE* chk = fopen(g_outs[sel].c_str(), "rb");
                if (!chk) {
                    MessageBoxA(hw, "File not yet converted", "Save As", MB_ICONWARNING);
                    return 0;
                }
                fclose(chk);
            }
            {
                char buf[MAX_PATH] = {};
                std::string bn = basename(g_outs[sel]);
                strncpy(buf, bn.c_str(), MAX_PATH - 1);

                OPENFILENAMEA ofn{};
                ofn.lStructSize = sizeof ofn;
                ofn.hwndOwner   = hw;
                ofn.lpstrFilter = "GIF Files\0*.gif\0";
                ofn.lpstrFile   = buf;
                ofn.nMaxFile    = MAX_PATH;
                ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                ofn.lpstrDefExt = "gif";

                if (GetSaveFileNameA(&ofn)) {
                    if (!CopyFileA(g_outs[sel].c_str(), buf, FALSE)) {
                        MessageBoxA(hw, "Failed to save file", "Error", MB_ICONERROR);
                    } else {
                        char st[MAX_PATH + 32];
                        snprintf(st, sizeof st, "Saved to: %s", buf);
                        SetDlgItemTextA(hw, ID_LBL_STATUS, st);
                    }
                }
            }
            return 0;
        }

        case ID_BTN_ZIP: {
            if (g_busy) {
                MessageBoxA(hw, "Wait for conversion to finish", "Busy", MB_ICONINFORMATION);
                return 0;
            }
            std::vector<std::string> existing;
            for (auto& p : g_outs) {
                FILE* chk = fopen(p.c_str(), "rb");
                if (chk) { fclose(chk); existing.push_back(p); }
            }
            if (existing.empty()) {
                MessageBoxA(hw, "No converted files to export", "Export ZIP", MB_ICONWARNING);
                return 0;
            }
            {
                char buf[MAX_PATH] = {};
                strncpy(buf, "converted.zip", MAX_PATH - 1);

                OPENFILENAMEA ofn{};
                ofn.lStructSize = sizeof ofn;
                ofn.hwndOwner   = hw;
                ofn.lpstrFilter = "ZIP Files\0*.zip\0";
                ofn.lpstrFile   = buf;
                ofn.nMaxFile    = MAX_PATH;
                ofn.Flags       = OFN_OVERWRITEPROMPT;
                ofn.lpstrDefExt = "zip";

                if (GetSaveFileNameA(&ofn)) {
                    ZipWriter zw;
                    for (auto& p : existing)
                        zw.add_file(basename(p).c_str(), p.c_str());
                    if (!zw.write(buf)) {
                        MessageBoxA(hw, "Failed to write ZIP file", "Error", MB_ICONERROR);
                    } else {
                        char st[128];
                        snprintf(st, sizeof st, "Exported %d GIFs to ZIP",
                                 (int)existing.size());
                        SetDlgItemTextA(hw, ID_LBL_STATUS, st);
                    }
                }
            }
            return 0;
        }
        }
        break;

    case WM_CONV_PROGRESS: {
        int idx = (int)wP, pct = (int)lP;
        SendDlgItemMessageA(hw, ID_PRG_FILE, PBM_SETPOS, pct, 0);
        int n = (int)g_files.size();
        int total_pct = (n > 0) ? (idx * 100 + pct) / n : 0;
        SendDlgItemMessageA(hw, ID_PRG_TOTAL, PBM_SETPOS, total_pct, 0);

        char st[64];
        snprintf(st, sizeof st, "Converting %d/%d (%d%%)", idx + 1, n, pct);
        SetDlgItemTextA(hw, ID_LBL_STATUS, st);
        return 0;
    }

    case WM_CONV_FILE_DONE: {
        int idx = (int)wP;
        HWND hLst = GetDlgItem(hw, ID_LST_FILES);
        std::string txt = basename(g_files[idx]) + " -> done";
        SendMessageA(hLst, LB_DELETESTRING, idx, 0);
        SendMessageA(hLst, LB_INSERTSTRING, idx, (LPARAM)txt.c_str());
        SendDlgItemMessageA(hw, ID_PRG_FILE, PBM_SETPOS, 100, 0);
        return 0;
    }

    case WM_CONV_ERROR: {
        int idx = (int)wP;
        char* emsg = (char*)lP;
        HWND hLst = GetDlgItem(hw, ID_LST_FILES);
        std::string txt = basename(g_files[idx]) + " -> ERROR";
        SendMessageA(hLst, LB_DELETESTRING, idx, 0);
        SendMessageA(hLst, LB_INSERTSTRING, idx, (LPARAM)txt.c_str());

        char buf[512];
        snprintf(buf, sizeof buf, "Error converting %s:\n%s",
                 basename(g_files[idx]).c_str(), emsg);
        MessageBoxA(hw, buf, "Conversion Error", MB_ICONERROR);
        free(emsg);
        return 0;
    }

    case WM_CONV_ALL_DONE: {
        EnableWindow(GetDlgItem(hw, ID_BTN_CONV), TRUE);
        EnableWindow(GetDlgItem(hw, ID_BTN_OPEN), TRUE);

        char st[64];
        snprintf(st, sizeof st, "Done - %d files converted", (int)g_files.size());
        SetDlgItemTextA(hw, ID_LBL_STATUS, st);

        SendDlgItemMessageA(hw, ID_PRG_FILE, PBM_SETPOS, 100, 0);
        SendDlgItemMessageA(hw, ID_PRG_TOTAL, PBM_SETPOS, 100, 0);
        g_busy = false;
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hw, msg, wP, lP);
}

// ---- WinMain ----
int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR, int nShow) {
    INITCOMMONCONTROLSEX icx{sizeof icx, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icx);

    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hI;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "MP4toGIF";
    RegisterClassExA(&wc);

    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    RECT rc = {0, 0, 520, 400};
    AdjustWindowRect(&rc, style, FALSE);

    g_hw = CreateWindowExA(0, "MP4toGIF", "MP4 to GIF Converter", style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           nullptr, nullptr, hI, nullptr);

    ShowWindow(g_hw, nShow);
    UpdateWindow(g_hw);

    MSG m;
    while (GetMessage(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
