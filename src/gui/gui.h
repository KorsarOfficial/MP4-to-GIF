#pragma once
#include <windows.h>
#include <commctrl.h>

enum : int {
    ID_BTN_OPEN   = 101, ID_BTN_CONV  = 102,
    ID_BTN_SAVEAS = 103, ID_BTN_ZIP   = 104,
    ID_EDT_FPS    = 201, ID_EDT_RESIZE = 202,
    ID_CMB_DITHER = 301, ID_CMB_QUANT  = 302,
    ID_LST_FILES  = 401,
    ID_PRG_FILE   = 501, ID_PRG_TOTAL  = 502,
    ID_LBL_STATUS = 601,
};

enum : UINT {
    WM_CONV_PROGRESS  = WM_APP + 1, // wParam=file_idx, lParam=percent(0-100)
    WM_CONV_FILE_DONE = WM_APP + 2, // wParam=file_idx
    WM_CONV_ALL_DONE  = WM_APP + 3,
    WM_CONV_ERROR     = WM_APP + 4, // wParam=file_idx, lParam=(char*)errmsg
};

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
