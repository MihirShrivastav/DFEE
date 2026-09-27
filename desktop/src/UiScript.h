#pragma once

#include <QString>

class QQmlApplicationEngine;

// Dev/test hook: drives the window from inside the process by posting synthetic
// events and calling the context objects -- never system-wide input. Run with
// QT_QPA_PLATFORM=offscreen. See desktop/tests/run_ui.ps1 for the step syntax.
void startUiScript(QQmlApplicationEngine& engine, const QString& spec);
