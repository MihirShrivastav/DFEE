#include "WindowChrome.h"

#include <QQuickWindow>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

void applyWindowChrome(QQuickWindow* window)
{
#ifdef Q_OS_WIN
    if (!window) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = TRUE;
    const COLORREF caption = RGB(0x24, 0x24, 0x26);   // Theme.toolbar #242426
    const COLORREF text = RGB(0xd4, 0xd4, 0xd8);      // Theme.text #d4d4d8
    // Attribute ids from the SDK enum (20, 34, 35, 36); spelled out so older SDK
    // headers without the Windows 11 names still compile.
    const HRESULT a = DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const HRESULT b = DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    const HRESULT c = DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    const HRESULT d = DwmSetWindowAttribute(hwnd, 34, &caption, sizeof(caption));
    if (FAILED(a) || FAILED(b) || FAILED(c) || FAILED(d)) {
        qInfo() << "WindowChrome: title bar coloring partly unsupported on this Windows build";
    }
#else
    Q_UNUSED(window);
#endif
}
