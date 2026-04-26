#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>

#include <vector>
#include <string>

namespace {

constexpr wchar_t kWindowClassName[] = L"LightStickerMainWindow";
constexpr wchar_t kIniSection[] = L"Sticker";
constexpr UINT kMsgEndEdit = WM_APP + 1;
constexpr int kMenuEditId = 1001;
constexpr int kMenuExitId = 1002;
constexpr int kResizeBorder = 8;

struct StickerState {
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    WNDPROC editOrigProc = nullptr;
    std::wstring text = L"Double-click to edit";
    int x = 120;
    int y = 120;
    int w = 320;
    int h = 200;
    std::wstring iniPath;
};

StickerState g_state;

std::wstring EscapeIniValue(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() * 2);
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            escaped += L"\\\\";
        } else if (ch == L'\n') {
            escaped += L"\\n";
        } else if (ch != L'\r') {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

std::wstring UnescapeIniValue(const std::wstring& value) {
    std::wstring unescaped;
    unescaped.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && (i + 1) < value.size()) {
            const wchar_t next = value[i + 1];
            if (next == L'\\') {
                unescaped.push_back(L'\\');
                ++i;
                continue;
            }
            if (next == L'n') {
                unescaped.push_back(L'\n');
                ++i;
                continue;
            }
        }
        unescaped.push_back(value[i]);
    }
    return unescaped;
}

std::wstring GetFallbackIniPath() {
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring path(modulePath);
    const size_t slashPos = path.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos) {
        path.resize(slashPos);
    }
    path += L"\\LightSticker.ini";
    return path;
}

std::wstring GetIniPath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        std::wstring dir(localAppData);
        CoTaskMemFree(localAppData);
        dir += L"\\LightSticker";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\settings.ini";
    }
    return GetFallbackIniPath();
}

void LoadState() {
    g_state.iniPath = GetIniPath();
    g_state.x = static_cast<int>(GetPrivateProfileIntW(kIniSection, L"x", g_state.x, g_state.iniPath.c_str()));
    g_state.y = static_cast<int>(GetPrivateProfileIntW(kIniSection, L"y", g_state.y, g_state.iniPath.c_str()));
    g_state.w = static_cast<int>(GetPrivateProfileIntW(kIniSection, L"w", g_state.w, g_state.iniPath.c_str()));
    g_state.h = static_cast<int>(GetPrivateProfileIntW(kIniSection, L"h", g_state.h, g_state.iniPath.c_str()));

    wchar_t textBuf[8192] = {};
    GetPrivateProfileStringW(kIniSection, L"text", L"Double-click to edit", textBuf, static_cast<DWORD>(std::size(textBuf)),
                             g_state.iniPath.c_str());
    g_state.text = UnescapeIniValue(textBuf);
}

void SaveState() {
    if (g_state.hwnd != nullptr) {
        RECT rect {};
        if (GetWindowRect(g_state.hwnd, &rect)) {
            g_state.x = rect.left;
            g_state.y = rect.top;
            g_state.w = rect.right - rect.left;
            g_state.h = rect.bottom - rect.top;
        }
    }

    WritePrivateProfileStringW(kIniSection, L"x", std::to_wstring(g_state.x).c_str(), g_state.iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"y", std::to_wstring(g_state.y).c_str(), g_state.iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"w", std::to_wstring(g_state.w).c_str(), g_state.iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"h", std::to_wstring(g_state.h).c_str(), g_state.iniPath.c_str());
    const std::wstring escaped = EscapeIniValue(g_state.text);
    WritePrivateProfileStringW(kIniSection, L"text", escaped.c_str(), g_state.iniPath.c_str());
}

HWND FindWorkerW() {
    const HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman != nullptr) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    }

    struct EnumData {
        HWND workerw = nullptr;
    } data;

    EnumWindows(
        [](HWND top, LPARAM lParam) -> BOOL {
            auto* enumData = reinterpret_cast<EnumData*>(lParam);
            if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) {
                enumData->workerw = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));

    if (data.workerw != nullptr) {
        return data.workerw;
    }
    return FindWindowW(L"WorkerW", nullptr);
}

void UpdatePositionFromWindow() {
    RECT rect {};
    if (g_state.hwnd != nullptr && GetWindowRect(g_state.hwnd, &rect)) {
        g_state.x = rect.left;
        g_state.y = rect.top;
        g_state.w = rect.right - rect.left;
        g_state.h = rect.bottom - rect.top;
    }
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostMessageW(GetParent(hwnd), kMsgEndEdit, 0, 0);
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        PostMessageW(GetParent(hwnd), kMsgEndEdit, 0, 0);
    }
    if (g_state.editOrigProc != nullptr) {
        return CallWindowProcW(g_state.editOrigProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EndEdit(bool save) {
    if (g_state.edit == nullptr) {
        return;
    }

    if (save) {
        const int len = GetWindowTextLengthW(g_state.edit);
        std::vector<wchar_t> buffer(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(g_state.edit, buffer.data(), len + 1);
        g_state.text.assign(buffer.data(), static_cast<size_t>(len));
    }

    DestroyWindow(g_state.edit);
    g_state.edit = nullptr;
    g_state.editOrigProc = nullptr;
    InvalidateRect(g_state.hwnd, nullptr, TRUE);
}

void BeginEdit() {
    if (g_state.hwnd == nullptr || g_state.edit != nullptr) {
        return;
    }

    RECT client {};
    GetClientRect(g_state.hwnd, &client);
    g_state.edit = CreateWindowExW(
        0, L"EDIT", g_state.text.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, client.right - client.left, client.bottom - client.top, g_state.hwnd, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_state.hwnd, GWLP_HINSTANCE)), nullptr);

    if (g_state.edit == nullptr) {
        return;
    }

    const LONG_PTR oldProc = SetWindowLongPtrW(g_state.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc));
    g_state.editOrigProc = reinterpret_cast<WNDPROC>(oldProc);
    SendMessageW(g_state.edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(g_state.edit, EM_SETSEL, 0, -1);
    SetFocus(g_state.edit);
}

int HitTestResize(const RECT& rect, const POINT& pt) {
    const bool left = pt.x < (rect.left + kResizeBorder);
    const bool right = pt.x >= (rect.right - kResizeBorder);
    const bool top = pt.y < (rect.top + kResizeBorder);
    const bool bottom = pt.y >= (rect.bottom - kResizeBorder);

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCAPTION;
}

POINT PointFromLParam(LPARAM lParam) {
    POINT pt {};
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    return pt;
}

void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kMenuEditId, L"Edit");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExitId, L"Exit");
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == kMenuEditId) {
        BeginEdit();
    } else if (cmd == kMenuExitId) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_state.hwnd = hwnd;
        return 0;
    case WM_NCHITTEST: {
        if (g_state.edit != nullptr) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        POINT pt = PointFromLParam(lParam);
        RECT rect {};
        GetWindowRect(hwnd, &rect);
        return HitTestResize(rect, pt);
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 140;
        mmi->ptMinTrackSize.y = 80;
        return 0;
    }
    case WM_MOVE:
    case WM_SIZE:
        UpdatePositionFromWindow();
        if (g_state.edit != nullptr) {
            RECT client {};
            GetClientRect(hwnd, &client);
            MoveWindow(g_state.edit, 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        BeginEdit();
        return 0;
    case WM_RBUTTONUP: {
        POINT pt = PointFromLParam(lParam);
        ClientToScreen(hwnd, &pt);
        ShowContextMenu(hwnd, pt);
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case kMsgEndEdit:
        EndEdit(true);
        SaveState();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client {};
        GetClientRect(hwnd, &client);
        HBRUSH brush = CreateSolidBrush(RGB(255, 248, 176));
        FillRect(hdc, &client, brush);
        DeleteObject(brush);

        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        InflateRect(&client, -10, -10);
        DrawTextW(hdc, g_state.text.c_str(), -1, &client, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        EndEdit(true);
        SaveState();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    LoadState();

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&wc) == 0) {
        return 1;
    }

    HWND workerw = FindWorkerW();
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, kWindowClassName, L"LightSticker",
        WS_VISIBLE | WS_POPUP, g_state.x, g_state.y, g_state.w, g_state.h,
        nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        return 1;
    }

    if (workerw != nullptr) {
        SetParent(hwnd, workerw);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
