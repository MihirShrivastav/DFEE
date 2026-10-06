#pragma once

class QQuickWindow;

// Colors the native Windows title bar to match the v2 toolbar (dark mode, caption,
// caption text and border; DWMWA_* per learn.microsoft.com DWMWINDOWATTRIBUTE).
// Caption/text/border colors need Windows 11 build 22000+; failures are ignored so
// older systems keep their default title bar. No-op on other platforms.
void applyWindowChrome(QQuickWindow* window);
