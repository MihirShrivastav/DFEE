#include "UiScript.h"

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

#include <memory>

namespace {

struct ScriptState {
    QQmlApplicationEngine* engine = nullptr;
    QQuickWindow* window = nullptr;
    QStringList steps;
    int failures = 0;
};

QVariant parseValue(const QString& text)
{
    if (text == QLatin1String("true")) return true;
    if (text == QLatin1String("false")) return false;
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (ok) return number;
    return text;
}

// Repeater/ListView delegates are visual children only (not QObject children), so
// findChild misses them; walk the item tree as well.
QQuickItem* findItem(QQuickItem* item, const QString& name)
{
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (QQuickItem* child : item->childItems()) {
        if (QQuickItem* hit = findItem(child, name)) return hit;
    }
    return nullptr;
}

QObject* resolveRoot(const ScriptState& s, const QString& name)
{
    const QVariant ctx = s.engine->rootContext()->contextProperty(name);
    if (auto* obj = ctx.value<QObject*>()) return obj;
    if (s.window->objectName() == name) return s.window;
    for (QObject* o : s.window->findChildren<QObject*>()) {
        if (o->objectName() == name) return o;
    }
    if (s.window->contentItem()) {
        if (auto* item = s.window->contentItem()->findChild<QQuickItem*>(name)) return item;
        if (auto* item = findItem(s.window->contentItem(), name)) return item;
    }
    return nullptr;
}

// "<root>.<a>.<b>..." -> value, walking QObject properties then QVariantMap keys.
QVariant readTarget(const ScriptState& s, const QString& target, bool* found)
{
    *found = false;
    const QStringList parts = target.split('.');
    QObject* root = resolveRoot(s, parts.value(0));
    if (!root || parts.size() < 2) return {};
    QVariant value = root->property(parts.at(1).toUtf8().constData());
    for (int i = 2; i < parts.size(); ++i) {
        const QString& key = parts.at(i);
        bool isIndex = false;
        const int index = key.toInt(&isIndex);
        if (key == QLatin1String("length") && value.canConvert<QVariantList>()) {
            value = value.toList().size();
        } else if (isIndex && value.canConvert<QVariantList>()) {
            const QVariantList list = value.toList();
            if (index < 0 || index >= list.size()) return {};
            value = list.at(index);
        } else if (value.canConvert<QVariantMap>()) {
            const QVariantMap map = value.toMap();
            if (!map.contains(key)) return {};
            value = map.value(key);
        } else {
            return {};
        }
    }
    *found = value.isValid();
    return value;
}

bool matches(const QVariant& actual, const QString& expected)
{
    bool okA = false, okE = false;
    const double a = actual.toString().toDouble(&okA);
    const double e = expected.toDouble(&okE);
    if (okA && okE) return qAbs(a - e) < 1e-6;
    return actual.toString() == expected;
}

void fail(ScriptState& s, const QString& step, const QString& actual)
{
    ++s.failures;
    qWarning().noquote() << "UISCRIPT FAIL" << step << "(got '" + actual + "')";
}

void postClick(ScriptState& s, const QString& spec, bool doubleClick = false,
               Qt::MouseButton button = Qt::LeftButton)
{
    const QString name = spec.section('@', 0, 0);
    const QString frac = spec.section('@', 1, 1);
    auto* item = qobject_cast<QQuickItem*>(resolveRoot(s, name));
    if (!item) {
        fail(s, (doubleClick ? "dclick:" : button == Qt::RightButton ? "rclick:" : "click:") + spec, "missing");
        return;
    }
    const double fx = frac.isEmpty() ? 0.5 : frac.section(',', 0, 0).toDouble();
    const double fy = frac.isEmpty() ? 0.5 : frac.section(',', 1, 1).toDouble();
    const QPointF p = item->mapToScene(QPointF(item->width() * fx, item->height() * fy));
    const QPointF g = s.window->mapToGlobal(p);
    // Delivered synchronously with increasing timestamps: Qt Quick tracks each press
    // and release against the device's persistent point state, which hand-posted
    // events with ts=0 corrupt (the release lost its scene position).
    static ulong timestamp = 1000;
    const auto post = [&](QEvent::Type type, Qt::MouseButtons held) {
        QMouseEvent event(type, p, p, g, button, held, Qt::NoModifier);
        event.setTimestamp(timestamp += 20);
        QCoreApplication::sendEvent(s.window, &event);
    };
    post(QEvent::MouseButtonPress, button);
    post(QEvent::MouseButtonRelease, Qt::NoButton);
    if (doubleClick) {  // Qt 6 order: press, release, press, dblclick, release
        post(QEvent::MouseButtonPress, Qt::LeftButton);
        post(QEvent::MouseButtonDblClick, Qt::LeftButton);
        post(QEvent::MouseButtonRelease, Qt::NoButton);
    }
}

// A mouse move with no button: drives HoverHandlers (enter on the target, leave
// on whatever was hovered before).
void postHover(ScriptState& s, const QString& spec)
{
    const QString name = spec.section('@', 0, 0);
    const QString frac = spec.section('@', 1, 1);
    auto* item = qobject_cast<QQuickItem*>(resolveRoot(s, name));
    if (!item) { fail(s, "hover:" + spec, "missing"); return; }
    const double fx = frac.isEmpty() ? 0.5 : frac.section(',', 0, 0).toDouble();
    const double fy = frac.isEmpty() ? 0.5 : frac.section(',', 1, 1).toDouble();
    const QPointF p = item->mapToScene(QPointF(item->width() * fx, item->height() * fy));
    static ulong timestamp = 500000;
    QMouseEvent event(QEvent::MouseMove, p, p, s.window->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    event.setTimestamp(timestamp += 20);
    QCoreApplication::sendEvent(s.window, &event);
}

void postKey(ScriptState& s, const QString& spec)
{
    const int key = spec.section('+', 0, 0).toInt();
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (spec.contains("+ctrl")) mods |= Qt::ControlModifier;
    if (spec.contains("+shift")) mods |= Qt::ShiftModifier;
    if (spec.contains("+alt")) mods |= Qt::AltModifier;
    const QString text = (key >= 0x20 && key < 0x7f && mods == Qt::NoModifier) ? QString(QChar(key)) : QString();
    QCoreApplication::postEvent(s.window, new QKeyEvent(QEvent::KeyPress, key, mods, text));
    QCoreApplication::postEvent(s.window, new QKeyEvent(QEvent::KeyRelease, key, mods, text));
}

void runNext(std::shared_ptr<ScriptState> s);

// Polls every 100 ms until <target>=<value> holds or the timeout expires.
void waitFor(std::shared_ptr<ScriptState> s, const QString& step, const QString& target,
             const QString& expected, int remainingMs)
{
    bool found = false;
    const QVariant actual = readTarget(*s, target, &found);
    if (found && matches(actual, expected)) { runNext(s); return; }
    if (remainingMs <= 0) {
        fail(*s, step, found ? actual.toString() : QStringLiteral("<missing>"));
        runNext(s);
        return;
    }
    QTimer::singleShot(100, s->window, [s, step, target, expected, remainingMs]() {
        waitFor(s, step, target, expected, remainingMs - 100);
    });
}

void runNext(std::shared_ptr<ScriptState> s)
{
    if (s->steps.isEmpty()) return;
    const QString step = s->steps.takeFirst().trimmed();
    int delay = 150;
    const QString verb = step.section(':', 0, 0);
    const QString arg = step.section(':', 1);
    QObject* engine = resolveRoot(*s, QStringLiteral("engine"));

    if (verb == QLatin1String("wait")) {
        delay = arg.toInt();
    } else if (verb == QLatin1String("click")) {
        postClick(*s, arg);
    } else if (verb == QLatin1String("dclick")) {
        postClick(*s, arg, true);
    } else if (verb == QLatin1String("rclick")) {  // rclick:<object>@fx,fy (right button)
        postClick(*s, arg, false, Qt::RightButton);
    } else if (verb == QLatin1String("hover")) {
        postHover(*s, arg);
    } else if (verb == QLatin1String("key")) {
        postKey(*s, arg);
    } else if (verb == QLatin1String("open")) {
        QMetaObject::invokeMethod(engine, "openFile", Q_ARG(QUrl, QUrl::fromLocalFile(arg)));
    } else if (verb == QLatin1String("stock")) {
        engine->setProperty("stock", arg);
    } else if (verb == QLatin1String("control")) {
        QMetaObject::invokeMethod(engine, "setFilmControl",
            Q_ARG(QString, arg.section('=', 0, 0)),
            Q_ARG(QVariant, parseValue(arg.section('=', 1))));
    } else if (verb == QLatin1String("crop")) {  // crop:x,y,w,h (normalised)
        const QStringList v = arg.split(',');
        QMetaObject::invokeMethod(engine, "setCrop",
            Q_ARG(double, v.value(0).toDouble()), Q_ARG(double, v.value(1).toDouble()),
            Q_ARG(double, v.value(2).toDouble()), Q_ARG(double, v.value(3).toDouble()));
    } else if (verb == QLatin1String("call")) {  // call:[<object>.]<method>=<string arg> (default: engine)
        const QString target = arg.section('=', 0, 0);
        QObject* obj = target.contains('.') ? resolveRoot(*s, target.section('.', 0, 0)) : engine;
        const QString method = target.contains('.') ? target.section('.', 1) : target;
        if (!obj || !QMetaObject::invokeMethod(obj, method.toUtf8().constData(),
                                               Q_ARG(QString, arg.section('=', 1)))) {
            fail(*s, step, QStringLiteral("no such method"));
        }
    } else if (verb == QLatin1String("set")) {  // set:<object>.<property>=<value>
        const QString target = arg.section('=', 0, 0);
        QObject* obj = resolveRoot(*s, target.section('.', 0, 0));
        if (!obj || !obj->setProperty(target.section('.', 1).toUtf8().constData(),
                                      parseValue(arg.section('=', 1)))) {
            fail(*s, step, "cannot set");
        }
    } else if (verb == QLatin1String("expect")) {
        bool found = false;
        const QString target = arg.section('=', 0, 0);
        const QString expected = arg.section('=', 1);
        const QVariant actual = readTarget(*s, target, &found);
        if (!found || !matches(actual, expected)) {
            fail(*s, step, found ? actual.toString() : QStringLiteral("<missing>"));
        }
    } else if (verb == QLatin1String("waitfor")) {
        const QString body = arg.section(',', 0, 0);
        const QString timeout = arg.section(',', 1, 1);
        waitFor(s, step, body.section('=', 0, 0), body.section('=', 1),
                timeout.isEmpty() ? 15000 : timeout.toInt());
        return;  // waitFor continues the script
    } else if (verb == QLatin1String("shot")) {
        const QImage img = s->window->grabWindow();
        if (img.isNull() || !img.save(arg)) fail(*s, step, "grab/save failed");
    } else if (verb == QLatin1String("quit")) {
        qInfo().noquote() << "UISCRIPT DONE failures=" + QString::number(s->failures);
        QCoreApplication::exit(s->failures > 0 ? 3 : 0);
        return;
    } else if (!step.isEmpty() && !step.startsWith('#')) {
        fail(*s, step, "unknown step");
    }
    qInfo().noquote() << "UISCRIPT" << step;
    QTimer::singleShot(delay, s->window, [s]() { runNext(s); });
}

}  // namespace

void startUiScript(QQmlApplicationEngine& engine, const QString& spec)
{
    auto s = std::make_shared<ScriptState>();
    s->engine = &engine;
    s->window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
    if (!s->window) { qWarning() << "UISCRIPT: no window"; return; }
    if (spec.startsWith('@')) {
        QFile f(spec.mid(1));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "UISCRIPT: cannot read" << spec.mid(1);
            QCoreApplication::exit(3);
            return;
        }
        for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
            const QString t = line.trimmed();
            if (!t.isEmpty() && !t.startsWith('#')) s->steps << t;
        }
    } else {
        s->steps = spec.split(';', Qt::SkipEmptyParts);
    }
    QTimer::singleShot(0, s->window, [s]() { runNext(s); });
}
