#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"LightStickerMainWindow";
constexpr wchar_t kGeneralSection[] = L"General";
constexpr wchar_t kLegacySection[] = L"Sticker";
constexpr UINT kMsgEndEdit = WM_APP + 1;
constexpr int kResizeBorder = 8;
constexpr COLORREF kTransparentKeyColor = RGB(1, 2, 3);
constexpr UINT kMaxStickerCount = 1024;

constexpr UINT kMenuEditId = 1001;
constexpr UINT kMenuExitId = 1002;
constexpr UINT kMenuNewId = 1003;
constexpr UINT kMenuDuplicateId = 1004;
constexpr UINT kMenuDeleteId = 1005;
constexpr UINT kMenuLockToggleId = 1006;

constexpr UINT kMenuThemeDefaultId = 1101;
constexpr UINT kMenuThemeMikuId = 1102;
constexpr UINT kMenuThemeTransparentId = 1103;

constexpr UINT kMenuFontDefaultId = 1201;
constexpr UINT kMenuFontConsolasId = 1202;

constexpr UINT kMenuSizeSmallId = 1301;
constexpr UINT kMenuSizeMediumId = 1302;
constexpr UINT kMenuSizeLargeId = 1303;

enum class Theme : int {
    Default = 0,
    Miku = 1,
    Transparent = 2,
};

enum class FontChoice : int {
    Default = 0,
    Consolas = 1,
};

enum class FontSize : int {
    Small = 0,
    Medium = 1,
    Large = 2,
};

constexpr int kMaxFontSizeIndex = static_cast<int>(FontSize::Large);

struct StickerState {
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    WNDPROC editOrigProc = nullptr;
    HFONT font = nullptr;

    std::wstring text = L"Double-click to edit";
    int x = 120;
    int y = 120;
    int w = 320;
    int h = 200;

    Theme theme = Theme::Default;
    FontChoice fontChoice = FontChoice::Default;
    FontSize fontSize = FontSize::Medium;
    bool locked = false;
};

struct AppState {
    std::wstring iniPath;
    std::vector<StickerState> stickers;
    HWND workerw = nullptr;
    bool exiting = false;
};

AppState g_app;

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

std::wstring GetModuleDirectory() {
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring path(modulePath);
    const size_t slashPos = path.find_last_of(L"\\/");
    if (slashPos != std::wstring::npos) {
        path.resize(slashPos);
    }
    return path;
}

bool EnsureDirectoryExists(const std::wstring& dir) {
    if (dir.empty()) {
        return false;
    }
    if (CreateDirectoryW(dir.c_str(), nullptr)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool CanWriteFilePath(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

std::wstring GetFallbackIniPath() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        return std::wstring();
    }

    std::wstring dir(localAppData);
    CoTaskMemFree(localAppData);
    dir += L"\\LightSticker";
    if (!EnsureDirectoryExists(dir)) {
        return std::wstring();
    }
    return dir + L"\\settings.ini";
}

std::wstring GetIniPath() {
    const std::wstring portablePath = GetModuleDirectory() + L"\\LightSticker.ini";
    if (CanWriteFilePath(portablePath)) {
        return portablePath;
    }

    const std::wstring fallback = GetFallbackIniPath();
    if (!fallback.empty() && CanWriteFilePath(fallback)) {
        return fallback;
    }

    return portablePath;
}

int ClampDimension(int value, int minimum) {
    return std::max(value, minimum);
}

Theme ParseTheme(int value) {
    switch (value) {
    case 1:
        return Theme::Miku;
    case 2:
        return Theme::Transparent;
    default:
        return Theme::Default;
    }
}

int ToThemeInt(Theme value) {
    return static_cast<int>(value);
}

FontChoice ParseFontChoice(int value) {
    return value == 1 ? FontChoice::Consolas : FontChoice::Default;
}

int ToFontChoiceInt(FontChoice value) {
    return static_cast<int>(value);
}

FontSize ParseFontSize(int value) {
    switch (value) {
    case 0:
        return FontSize::Small;
    case 2:
        return FontSize::Large;
    default:
        return FontSize::Medium;
    }
}

int ToFontSizeInt(FontSize value) {
    return static_cast<int>(value);
}

int FontSizeToPoint(FontSize value) {
    switch (value) {
    case FontSize::Small:
        return 14;
    case FontSize::Large:
        return 24;
    default:
        return 18;
    }
}

StickerState MakeDefaultSticker() {
    StickerState sticker;
    return sticker;
}

std::wstring StickerSectionName(int index) {
    return L"Sticker" + std::to_wstring(index);
}

void LoadState() {
    g_app.iniPath = GetIniPath();

    const UINT count = GetPrivateProfileIntW(kGeneralSection, L"StickerCount", 0, g_app.iniPath.c_str());
    g_app.stickers.clear();

    if (count > kMaxStickerCount) {
        g_app.stickers.push_back(MakeDefaultSticker());
        return;
    }

    if (count == 0) {
        StickerState legacy = MakeDefaultSticker();
        legacy.x = static_cast<int>(GetPrivateProfileIntW(kLegacySection, L"x", legacy.x, g_app.iniPath.c_str()));
        legacy.y = static_cast<int>(GetPrivateProfileIntW(kLegacySection, L"y", legacy.y, g_app.iniPath.c_str()));
        legacy.w = ClampDimension(static_cast<int>(GetPrivateProfileIntW(kLegacySection, L"w", legacy.w, g_app.iniPath.c_str())), 140);
        legacy.h = ClampDimension(static_cast<int>(GetPrivateProfileIntW(kLegacySection, L"h", legacy.h, g_app.iniPath.c_str())), 80);

        wchar_t textBuf[8192] = {};
        GetPrivateProfileStringW(kLegacySection, L"text", legacy.text.c_str(), textBuf,
                                 static_cast<DWORD>(std::size(textBuf)), g_app.iniPath.c_str());
        legacy.text = UnescapeIniValue(textBuf);
        g_app.stickers.push_back(std::move(legacy));
        return;
    }

    for (UINT i = 0; i < count; ++i) {
        StickerState sticker = MakeDefaultSticker();
        const std::wstring section = StickerSectionName(static_cast<int>(i));
        sticker.x = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"x", sticker.x, g_app.iniPath.c_str()));
        sticker.y = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"y", sticker.y, g_app.iniPath.c_str()));
        sticker.w = ClampDimension(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"w", sticker.w, g_app.iniPath.c_str())), 140);
        sticker.h = ClampDimension(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"h", sticker.h, g_app.iniPath.c_str())), 80);
        sticker.theme = ParseTheme(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"theme", 0, g_app.iniPath.c_str())));
        sticker.fontChoice = ParseFontChoice(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"font", 0, g_app.iniPath.c_str())));
        sticker.fontSize = ParseFontSize(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"size", 1, g_app.iniPath.c_str())));
        sticker.locked = GetPrivateProfileIntW(section.c_str(), L"locked", 0, g_app.iniPath.c_str()) != 0;

        wchar_t textBuf[8192] = {};
        GetPrivateProfileStringW(section.c_str(), L"text", sticker.text.c_str(), textBuf,
                                 static_cast<DWORD>(std::size(textBuf)), g_app.iniPath.c_str());
        sticker.text = UnescapeIniValue(textBuf);
        g_app.stickers.push_back(std::move(sticker));
    }

    if (g_app.stickers.empty()) {
        g_app.stickers.push_back(MakeDefaultSticker());
    }
}

void UpdatePositionFromWindow(StickerState& sticker) {
    RECT rect {};
    if (sticker.hwnd != nullptr && GetWindowRect(sticker.hwnd, &rect)) {
        sticker.x = rect.left;
        sticker.y = rect.top;
        sticker.w = rect.right - rect.left;
        sticker.h = rect.bottom - rect.top;
    }
}

void SaveAllState() {
    for (auto& sticker : g_app.stickers) {
        UpdatePositionFromWindow(sticker);
    }

    const UINT oldCount = GetPrivateProfileIntW(kGeneralSection, L"StickerCount", 0, g_app.iniPath.c_str());
    for (UINT i = 0; i < oldCount; ++i) {
        const std::wstring section = StickerSectionName(static_cast<int>(i));
        WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, g_app.iniPath.c_str());
    }
    WritePrivateProfileStringW(kLegacySection, nullptr, nullptr, g_app.iniPath.c_str());

    WritePrivateProfileStringW(kGeneralSection, L"StickerCount", std::to_wstring(g_app.stickers.size()).c_str(), g_app.iniPath.c_str());

    for (size_t i = 0; i < g_app.stickers.size(); ++i) {
        const StickerState& sticker = g_app.stickers[i];
        const std::wstring section = StickerSectionName(static_cast<int>(i));

        WritePrivateProfileStringW(section.c_str(), L"x", std::to_wstring(sticker.x).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"y", std::to_wstring(sticker.y).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"w", std::to_wstring(sticker.w).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"h", std::to_wstring(sticker.h).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"theme", std::to_wstring(ToThemeInt(sticker.theme)).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"font", std::to_wstring(ToFontChoiceInt(sticker.fontChoice)).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"size", std::to_wstring(ToFontSizeInt(sticker.fontSize)).c_str(), g_app.iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"locked", sticker.locked ? L"1" : L"0", g_app.iniPath.c_str());
        const std::wstring escaped = EscapeIniValue(sticker.text);
        WritePrivateProfileStringW(section.c_str(), L"text", escaped.c_str(), g_app.iniPath.c_str());
    }
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

StickerState* FindStickerByHwnd(HWND hwnd) {
    for (auto& sticker : g_app.stickers) {
        if (sticker.hwnd == hwnd) {
            return &sticker;
        }
    }
    return nullptr;
}

StickerState* FindStickerByEdit(HWND edit) {
    for (auto& sticker : g_app.stickers) {
        if (sticker.edit == edit) {
            return &sticker;
        }
    }
    return nullptr;
}

HFONT CreateStickerFont(const StickerState& sticker, HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    bool releaseScreenDc = false;
    if (hdc == nullptr) {
        hdc = GetDC(nullptr);
        releaseScreenDc = true;
    }
    const int dpi = hdc != nullptr ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc != nullptr) {
        ReleaseDC(releaseScreenDc ? nullptr : hwnd, hdc);
    }

    LOGFONTW lf {};
    lf.lfHeight = -MulDiv(FontSizeToPoint(sticker.fontSize), dpi, 72);
    lf.lfQuality = CLEARTYPE_QUALITY;

    if (sticker.fontChoice == FontChoice::Consolas) {
        lstrcpyW(lf.lfFaceName, L"Consolas");
    }

    return CreateFontIndirectW(&lf);
}

void ApplyStickerFont(StickerState& sticker) {
    if (sticker.font != nullptr) {
        DeleteObject(sticker.font);
        sticker.font = nullptr;
    }
    sticker.font = CreateStickerFont(sticker, sticker.hwnd);
    if (sticker.edit != nullptr && sticker.font != nullptr) {
        SendMessageW(sticker.edit, WM_SETFONT, reinterpret_cast<WPARAM>(sticker.font), TRUE);
    }
}

COLORREF GetBackgroundColor(Theme theme) {
    switch (theme) {
    case Theme::Miku:
        return RGB(57, 197, 187);
    case Theme::Transparent:
        return kTransparentKeyColor;
    default:
        return RGB(255, 248, 176);
    }
}

COLORREF GetTextColor(Theme theme) {
    if (theme == Theme::Transparent) {
        return RGB(245, 245, 245);
    }
    return RGB(20, 20, 20);
}

void ApplyThemeWindowStyle(StickerState& sticker) {
    if (sticker.hwnd == nullptr) {
        return;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(sticker.hwnd, GWL_EXSTYLE);
    const bool transparent = sticker.theme == Theme::Transparent;
    if (transparent) {
        exStyle |= WS_EX_LAYERED;
    } else {
        exStyle &= ~static_cast<LONG_PTR>(WS_EX_LAYERED);
    }

    SetWindowLongPtrW(sticker.hwnd, GWL_EXSTYLE, exStyle);
    if (transparent) {
        SetLayeredWindowAttributes(sticker.hwnd, kTransparentKeyColor, 0, LWA_COLORKEY);
    }

    SetWindowPos(sticker.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(sticker.hwnd, nullptr, TRUE);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StickerState* sticker = FindStickerByEdit(hwnd);

    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostMessageW(GetParent(hwnd), kMsgEndEdit, 0, 0);
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        PostMessageW(GetParent(hwnd), kMsgEndEdit, 0, 0);
    }

    if (sticker != nullptr && sticker->editOrigProc != nullptr) {
        return CallWindowProcW(sticker->editOrigProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EndEdit(StickerState& sticker, bool save) {
    if (sticker.edit == nullptr) {
        return;
    }

    if (save) {
        const int len = GetWindowTextLengthW(sticker.edit);
        std::vector<wchar_t> buffer(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(sticker.edit, buffer.data(), len + 1);
        sticker.text.assign(buffer.data(), static_cast<size_t>(len));
    }

    DestroyWindow(sticker.edit);
    sticker.edit = nullptr;
    sticker.editOrigProc = nullptr;
    if (sticker.hwnd != nullptr) {
        InvalidateRect(sticker.hwnd, nullptr, TRUE);
    }
}

void BeginEdit(StickerState& sticker) {
    if (sticker.hwnd == nullptr || sticker.edit != nullptr || sticker.locked) {
        return;
    }

    RECT client {};
    GetClientRect(sticker.hwnd, &client);
    sticker.edit = CreateWindowExW(
        0, L"EDIT", sticker.text.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, client.right - client.left, client.bottom - client.top, sticker.hwnd, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(sticker.hwnd, GWLP_HINSTANCE)), nullptr);

    if (sticker.edit == nullptr) {
        return;
    }

    const LONG_PTR oldProc = SetWindowLongPtrW(sticker.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc));
    sticker.editOrigProc = reinterpret_cast<WNDPROC>(oldProc);

    if (sticker.font == nullptr) {
        ApplyStickerFont(sticker);
    }
    SendMessageW(sticker.edit, WM_SETFONT, reinterpret_cast<WPARAM>(sticker.font), TRUE);
    SendMessageW(sticker.edit, EM_SETSEL, 0, -1);
    SetFocus(sticker.edit);
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

void AdjustSizeChoice(StickerState& sticker, int delta) {
    int index = ToFontSizeInt(sticker.fontSize);
    index = std::clamp(index + delta, 0, kMaxFontSizeIndex);
    const FontSize newSize = ParseFontSize(index);
    if (newSize == sticker.fontSize) {
        return;
    }
    sticker.fontSize = newSize;
    ApplyStickerFont(sticker);
    if (sticker.hwnd != nullptr) {
        InvalidateRect(sticker.hwnd, nullptr, TRUE);
    }
    SaveAllState();
}

bool CreateStickerWindow(StickerState& sticker, HINSTANCE instance);

void RemoveStickerByHwnd(HWND hwnd) {
    auto it = std::find_if(g_app.stickers.begin(), g_app.stickers.end(),
                           [hwnd](const StickerState& s) { return s.hwnd == hwnd; });
    if (it == g_app.stickers.end()) {
        return;
    }

    if (it->font != nullptr) {
        DeleteObject(it->font);
        it->font = nullptr;
    }
    g_app.stickers.erase(it);
}

void DeleteSticker(StickerState& sticker) {
    if (g_app.stickers.size() <= 1) {
        EndEdit(sticker, true);
        sticker.text.clear();
        if (sticker.hwnd != nullptr) {
            InvalidateRect(sticker.hwnd, nullptr, TRUE);
        }
        SaveAllState();
        return;
    }

    HWND target = sticker.hwnd;
    EndEdit(sticker, true);
    if (target != nullptr) {
        DestroyWindow(target);
    }
    SaveAllState();
}

void BeginExitAll() {
    if (g_app.exiting) {
        return;
    }
    g_app.exiting = true;

    for (auto& sticker : g_app.stickers) {
        EndEdit(sticker, true);
    }
    SaveAllState();

    std::vector<HWND> windows;
    windows.reserve(g_app.stickers.size());
    for (const auto& sticker : g_app.stickers) {
        if (sticker.hwnd != nullptr) {
            windows.push_back(sticker.hwnd);
        }
    }

    for (HWND hwnd : windows) {
        DestroyWindow(hwnd);
    }
}

void HandleMenuCommand(HWND hwnd, UINT cmd) {
    StickerState* sticker = FindStickerByHwnd(hwnd);
    if (sticker == nullptr) {
        return;
    }

    switch (cmd) {
    case kMenuEditId:
        if (!sticker->locked) {
            BeginEdit(*sticker);
        }
        break;
    case kMenuNewId: {
        StickerState created = MakeDefaultSticker();
        created.x = sticker->x + 30;
        created.y = sticker->y + 30;
        g_app.stickers.push_back(std::move(created));
        StickerState& newest = g_app.stickers.back();
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        if (!CreateStickerWindow(newest, instance)) {
            g_app.stickers.pop_back();
            return;
        }
        SaveAllState();
        break;
    }
    case kMenuDuplicateId: {
        StickerState duplicated = *sticker;
        duplicated.hwnd = nullptr;
        duplicated.edit = nullptr;
        duplicated.editOrigProc = nullptr;
        duplicated.font = nullptr;
        duplicated.x += 30;
        duplicated.y += 30;
        g_app.stickers.push_back(std::move(duplicated));
        StickerState& newest = g_app.stickers.back();
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        if (!CreateStickerWindow(newest, instance)) {
            g_app.stickers.pop_back();
            return;
        }
        SaveAllState();
        break;
    }
    case kMenuDeleteId:
        DeleteSticker(*sticker);
        break;
    case kMenuLockToggleId:
        sticker->locked = !sticker->locked;
        if (sticker->locked) {
            EndEdit(*sticker, true);
        }
        SaveAllState();
        break;
    case kMenuThemeDefaultId:
        sticker->theme = Theme::Default;
        ApplyThemeWindowStyle(*sticker);
        SaveAllState();
        break;
    case kMenuThemeMikuId:
        sticker->theme = Theme::Miku;
        ApplyThemeWindowStyle(*sticker);
        SaveAllState();
        break;
    case kMenuThemeTransparentId:
        sticker->theme = Theme::Transparent;
        ApplyThemeWindowStyle(*sticker);
        SaveAllState();
        break;
    case kMenuFontDefaultId:
        sticker->fontChoice = FontChoice::Default;
        ApplyStickerFont(*sticker);
        InvalidateRect(sticker->hwnd, nullptr, TRUE);
        SaveAllState();
        break;
    case kMenuFontConsolasId:
        sticker->fontChoice = FontChoice::Consolas;
        ApplyStickerFont(*sticker);
        InvalidateRect(sticker->hwnd, nullptr, TRUE);
        SaveAllState();
        break;
    case kMenuSizeSmallId:
        sticker->fontSize = FontSize::Small;
        ApplyStickerFont(*sticker);
        InvalidateRect(sticker->hwnd, nullptr, TRUE);
        SaveAllState();
        break;
    case kMenuSizeMediumId:
        sticker->fontSize = FontSize::Medium;
        ApplyStickerFont(*sticker);
        InvalidateRect(sticker->hwnd, nullptr, TRUE);
        SaveAllState();
        break;
    case kMenuSizeLargeId:
        sticker->fontSize = FontSize::Large;
        ApplyStickerFont(*sticker);
        InvalidateRect(sticker->hwnd, nullptr, TRUE);
        SaveAllState();
        break;
    case kMenuExitId:
        BeginExitAll();
        break;
    default:
        break;
    }
}

void ShowContextMenu(HWND hwnd, const StickerState& sticker, POINT pt) {
    HMENU menu = CreatePopupMenu();
    HMENU themeMenu = CreatePopupMenu();
    HMENU fontMenu = CreatePopupMenu();
    HMENU sizeMenu = CreatePopupMenu();

    if (menu == nullptr || themeMenu == nullptr || fontMenu == nullptr || sizeMenu == nullptr) {
        if (sizeMenu != nullptr) DestroyMenu(sizeMenu);
        if (fontMenu != nullptr) DestroyMenu(fontMenu);
        if (themeMenu != nullptr) DestroyMenu(themeMenu);
        if (menu != nullptr) DestroyMenu(menu);
        return;
    }

    AppendMenuW(themeMenu, MF_STRING | (sticker.theme == Theme::Default ? MF_CHECKED : 0), kMenuThemeDefaultId, L"Pale Yellow");
    AppendMenuW(themeMenu, MF_STRING | (sticker.theme == Theme::Miku ? MF_CHECKED : 0), kMenuThemeMikuId, L"Miku");
    AppendMenuW(themeMenu, MF_STRING | (sticker.theme == Theme::Transparent ? MF_CHECKED : 0), kMenuThemeTransparentId,
                L"Transparent");

    AppendMenuW(fontMenu, MF_STRING | (sticker.fontChoice == FontChoice::Default ? MF_CHECKED : 0), kMenuFontDefaultId, L"Default");
    AppendMenuW(fontMenu, MF_STRING | (sticker.fontChoice == FontChoice::Consolas ? MF_CHECKED : 0), kMenuFontConsolasId,
                L"Consolas");

    AppendMenuW(sizeMenu, MF_STRING | (sticker.fontSize == FontSize::Small ? MF_CHECKED : 0), kMenuSizeSmallId, L"Small");
    AppendMenuW(sizeMenu, MF_STRING | (sticker.fontSize == FontSize::Medium ? MF_CHECKED : 0), kMenuSizeMediumId, L"Medium");
    AppendMenuW(sizeMenu, MF_STRING | (sticker.fontSize == FontSize::Large ? MF_CHECKED : 0), kMenuSizeLargeId, L"Large");

    AppendMenuW(menu, MF_STRING | (sticker.locked ? MF_GRAYED : 0), kMenuEditId, L"Edit");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuNewId, L"New");
    AppendMenuW(menu, MF_STRING, kMenuDuplicateId, L"Duplicate");
    AppendMenuW(menu, MF_STRING, kMenuDeleteId, L"Delete");
    AppendMenuW(menu, MF_STRING, kMenuLockToggleId, sticker.locked ? L"Unlock" : L"Lock");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"Theme");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fontMenu), L"Font");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizeMenu), L"Size");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExitId, L"Exit");

    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd != 0) {
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StickerState* sticker = FindStickerByHwnd(hwnd);

    switch (msg) {
    case WM_NCHITTEST: {
        if (sticker == nullptr) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        if (sticker->locked) {
            return HTCLIENT;
        }
        if (sticker->edit != nullptr) {
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
        if (sticker != nullptr) {
            UpdatePositionFromWindow(*sticker);
            if (sticker->edit != nullptr) {
                RECT client {};
                GetClientRect(hwnd, &client);
                MoveWindow(sticker->edit, 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
            }
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (sticker != nullptr && !sticker->locked) {
            BeginEdit(*sticker);
        }
        return 0;
    case WM_RBUTTONUP:
        if (sticker != nullptr) {
            POINT pt = PointFromLParam(lParam);
            ClientToScreen(hwnd, &pt);
            ShowContextMenu(hwnd, *sticker, pt);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (sticker != nullptr && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            AdjustSizeChoice(*sticker, delta > 0 ? 1 : -1);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) {
            BeginExitAll();
            return 0;
        }
        break;
    case WM_COMMAND:
        HandleMenuCommand(hwnd, LOWORD(wParam));
        return 0;
    case kMsgEndEdit:
        if (sticker != nullptr) {
            EndEdit(*sticker, true);
            SaveAllState();
        }
        return 0;
    case WM_PAINT:
        if (sticker != nullptr) {
            PAINTSTRUCT ps {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client {};
            GetClientRect(hwnd, &client);

            HBRUSH brush = CreateSolidBrush(GetBackgroundColor(sticker->theme));
            FillRect(hdc, &client, brush);
            DeleteObject(brush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetTextColor(sticker->theme));
            if (sticker->font == nullptr) {
                ApplyStickerFont(*sticker);
            }
            if (sticker->font != nullptr) {
                SelectObject(hdc, sticker->font);
            }
            InflateRect(&client, -10, -10);
            DrawTextW(hdc, sticker->text.c_str(), -1, &client, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_CLOSE:
        BeginExitAll();
        return 0;
    case WM_NCDESTROY:
        RemoveStickerByHwnd(hwnd);
        if (g_app.stickers.empty()) {
            PostQuitMessage(0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool CreateStickerWindow(StickerState& sticker, HINSTANCE instance) {
    const DWORD exStyle = WS_EX_TOOLWINDOW | (sticker.theme == Theme::Transparent ? WS_EX_LAYERED : 0);
    HWND hwnd = CreateWindowExW(
        exStyle, kWindowClassName, L"LightSticker",
        WS_VISIBLE | WS_POPUP,
        sticker.x, sticker.y, sticker.w, sticker.h,
        nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        return false;
    }

    sticker.hwnd = hwnd;
    if (g_app.workerw != nullptr) {
        SetParent(hwnd, g_app.workerw);
    }

    ApplyStickerFont(sticker);
    ApplyThemeWindowStyle(sticker);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
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

    g_app.workerw = FindWorkerW();

    bool createdAny = false;
    for (auto& sticker : g_app.stickers) {
        if (CreateStickerWindow(sticker, instance)) {
            createdAny = true;
        }
    }

    if (!createdAny) {
        g_app.stickers.clear();
        g_app.stickers.push_back(MakeDefaultSticker());
        if (!CreateStickerWindow(g_app.stickers.front(), instance)) {
            return 1;
        }
    }

    MSG msg {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
