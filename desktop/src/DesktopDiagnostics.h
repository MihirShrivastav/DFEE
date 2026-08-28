#pragma once

#include <QString>

namespace dfee::desktop {

// Installs a process-wide Qt message handler that writes a rotated diagnostic
// log under the current user's local app-data directory. Call once after the
// QGuiApplication exists and before the desktop controllers are constructed.
void installApplicationLogging();

// Exposed for support instructions and failure messages. The directory is
// created by installApplicationLogging().
QString diagnosticLogDirectory();

}  // namespace dfee::desktop
