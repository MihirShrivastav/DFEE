#include "DesktopDiagnostics.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <cstdio>
#include <mutex>

namespace dfee::desktop {
namespace {

constexpr int kMaximumLogFiles = 8;
QtMessageHandler g_previous_handler = nullptr;
std::FILE* g_log_file = nullptr;
std::mutex g_log_mutex;

QString messageTypeName(const QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void writeDiagnosticMessage(
    QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString category = context.category == nullptr || context.category[0] == '\0'
        ? QStringLiteral("default")
        : QString::fromUtf8(context.category);
    const QString line = QStringLiteral("%1 | %2 | %3 | %4\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             messageTypeName(type), category, message);
    const QByteArray utf8 = line.toUtf8();

    {
        const std::scoped_lock lock(g_log_mutex);
        if (g_log_file != nullptr) {
            std::fwrite(utf8.constData(), 1, static_cast<std::size_t>(utf8.size()), g_log_file);
            // Keep warnings and failures durable even if a later crash terminates
            // the process before its normal Qt shutdown path.
            if (type >= QtWarningMsg) {
                std::fflush(g_log_file);
            }
        }
    }

    if (g_previous_handler != nullptr) {
        g_previous_handler(type, context, message.toUtf8().constData());
    }
}

void pruneOldLogs(const QDir& directory)
{
    QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("film-lab-*.log")}, QDir::Files, QDir::Time | QDir::Reversed);
    while (files.size() > kMaximumLogFiles) {
        QFile::remove(files.takeFirst().absoluteFilePath());
    }
}

}  // namespace

QString diagnosticLogDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/Logs");
}

void installApplicationLogging()
{
    const QDir directory(diagnosticLogDirectory());
    if (!QDir().mkpath(directory.absolutePath())) {
        return;
    }
    pruneOldLogs(directory);

    const QString fileName = QStringLiteral("film-lab-%1-%2.log")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
        .arg(QCoreApplication::applicationPid());
    const QString path = directory.filePath(fileName);

    {
        const std::scoped_lock lock(g_log_mutex);
        g_log_file = _wfopen(reinterpret_cast<const wchar_t*>(path.utf16()), L"ab");
    }
    g_previous_handler = qInstallMessageHandler(writeDiagnosticMessage);
    qInfo().noquote() << "Film Lab logging started:" << path;
}

}  // namespace dfee::desktop
