#include "ExportNaming.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace ExportNaming {

namespace {
constexpr int kMaxStem = 180;

bool isSeparator(QChar c)
{
    return c == QLatin1Char(' ') || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.');
}
}  // namespace

QString expand(const QString& pattern, const Tokens& tokens)
{
    QString out = pattern;
    out.replace(QStringLiteral("{name}"), tokens.name);
    out.replace(QStringLiteral("{film}"), tokens.film);
    out.replace(QStringLiteral("{date}"), tokens.date);
    out.replace(QStringLiteral("{camera}"), tokens.camera);
    out.replace(QStringLiteral("{seq}"), QStringLiteral("%1").arg(tokens.sequence, 4, 10, QLatin1Char('0')));
    // An empty token leaves its separators behind ("DSC0421_" for "{name}_{film}"
    // without a film): collapse a run of one separator and trim them from the ends.
    static const QRegularExpression repeated(QStringLiteral("([ _.-])\\1+"));
    out.replace(repeated, QStringLiteral("\\1"));
    static const QRegularExpression spacedDash(QStringLiteral("^\\s*-\\s*|\\s*-\\s*$"));
    out.replace(spacedDash, QString());
    while (!out.isEmpty() && isSeparator(out.front())) out.remove(0, 1);
    while (!out.isEmpty() && isSeparator(out.back())) out.chop(1);
    return sanitize(out);
}

QString sanitize(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const bool illegal = c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c);
        out.append(illegal ? QLatin1Char('-') : c);
    }
    out = out.trimmed();
    while (!out.isEmpty() && (out.back() == QLatin1Char('.') || out.back() == QLatin1Char(' '))) out.chop(1);
    if (out.size() > kMaxStem) out.truncate(kMaxStem);
    if (out.isEmpty()) return QStringLiteral("export");
    static const QRegularExpression reserved(
        QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$"), QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(out).hasMatch()) out.append(QLatin1Char('_'));
    return out;
}

QString extensionFor(const QString& format)
{
    if (format == QLatin1String("jpeg")) return QStringLiteral(".jpg");
    if (format == QLatin1String("tiff")) return QStringLiteral(".tif");
    return QStringLiteral(".png");
}

Collision collisionFromString(const QString& rule)
{
    if (rule == QLatin1String("replace")) return Collision::Replace;
    if (rule == QLatin1String("skip")) return Collision::Skip;
    return Collision::AddNumber;
}

Target resolveTarget(const QString& folder, const QString& stem, const QString& extension, Collision rule)
{
    const QDir dir(folder);
    Target t;
    t.fileName = stem + extension;
    t.path = dir.filePath(t.fileName);
    t.exists = QFileInfo::exists(t.path);
    if (!t.exists || rule == Collision::Replace) return t;
    if (rule == Collision::Skip) {
        t.skip = true;
        return t;
    }
    for (int n = 2; n < 10000; ++n) {
        const QString candidate = stem + QLatin1Char('-') + QString::number(n) + extension;
        if (!QFileInfo::exists(dir.filePath(candidate))) {
            t.fileName = candidate;
            t.path = dir.filePath(candidate);
            return t;
        }
    }
    t.skip = true;  // thousands of copies: refuse rather than loop forever
    return t;
}

}  // namespace ExportNaming
