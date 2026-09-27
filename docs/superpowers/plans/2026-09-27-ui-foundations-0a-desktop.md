# UI Foundations 0A (desktop) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the desktop app per-photo edit memory (fixing today's look carry-over), camera metadata for the UI, a stock catalog for the Films browser, and a headless UI-script harness that can verify all of it.

**Architecture:** A SQLite-backed `EditStore` (Qt6::Sql) persists each photo's recipe and history, loaded by `EngineController` before a photo's first preview request and saved debounced after edits; it is bypassed in Lightroom mode. `RenderWorker` forwards the decoded `NativeRawMetadata` as an `imageInfo` map. A desktop-side `catalog.json` adds group/ISO/blurb to the stock model. The in-process `DFEE_UI_SCRIPT` driver moves to `UiScript.cpp` and gains open/control/expect/waitfor/shot/quit steps, driven by `desktop/tests/run_ui.ps1`.

**Tech Stack:** C++20, Qt 6.8.3 (msvc2022_64: Quick, Sql, Test), MSVC, CMake (VS 2022 generator, build dir `desktop/out/build`).

**Spec:** `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (Phase 0: 0e metadata, 0f EditStore, 0g stock catalog, 0h UI scripts). Plans 0B (QML split) and 0C (engine: golden hashes, profile cache, proxy render) are separate.

## Global Constraints

- No visible UI change in this plan except that edits no longer carry over to the next photo.
- A never-edited photo opens clean: stock `none`, all controls at `defaultFilmControls()`.
- Lightroom mode (`lightroomRoundTrip_`) never reads or writes the store.
- Tests never touch the user's real catalog: `DFEE_CATALOG_PATH` overrides the store location.
- Never drive the app with system-wide input; UI checks run offscreen through `DFEE_UI_SCRIPT` only.
- US spelling in user-facing strings ("Color").
- No change to engine rendering (`cpp_engine/`) in this plan.
- After each shipped task: rebuild Release (`cmake --build desktop/out/build --config Release`).
- Commit messages carry no Co-Authored-By lines.
- Deferred to later phases (schema_version 2): re-attaching edits after a folder move (content signature), rating/flag/thumbnail columns.
- Local sample photos (not committed): `E:/new_raws/3071874357.arw` (Sony ILCE-7CR), `E:/new_raws/7033866904.arw` (Sony ILCE-7RM5), `E:/new_raws/3071874357.tif` (Lightroom TIFF).

## Review Focus

1. **Open B while the worker is still rendering A** (queued open): A's edits must be saved and B must open clean — the load happens at deferred dispatch, not in `openFile`. Pinned by `memory.script` (opens B immediately after an edit).
2. **A stored record from an older schema** (missing keys, unknown keys, int-vs-double values after JSON): must merge onto defaults, drop unknown keys, and not count as "edited" when equal to defaults. Pinned in Task 3 (`sameControls`, sparse round-trip) and Task 2 unit tests.
3. **The catalog database cannot be opened** (read-only folder, corrupt file): the app must still work, just without memory. Pinned by `EditStoreTest::unopenableStoreIsHarmless`.
4. **The same photo reached by a different path spelling** (case, backslashes): must hit the same record. Pinned by `EditStoreTest::pathKeyNormalises`.
5. **An Auto-grain result that arrives after switching photos** must be ignored, not written into the new photo. Guarded in Task 3 (`grainRequestFile_`); checked by code review (timing-dependent to script).

---

### Task 1: UI-script harness (UiScript.cpp + run_ui.ps1)

**Files:**
- Create: `desktop/src/UiScript.h`, `desktop/src/UiScript.cpp`
- Create: `desktop/tests/run_ui.ps1`, `desktop/tests/ui/smoke.script`
- Modify: `desktop/src/main.cpp` (remove `runUiScript`, call `startUiScript`)
- Modify: `desktop/CMakeLists.txt` (add sources)

**Interfaces:**
- Produces: `void startUiScript(QQmlApplicationEngine& engine, const QString& spec);` — `spec` is a `;`-separated step list, or `@<path>` to a file with one step per line (`#` comments). Steps:
  - `wait:<ms>`
  - `click:<objectName>[@fx,fy]`, `key:<Qt::Key int>[+ctrl]` (existing behaviour)
  - `open:<path>` → `engine.openFile(QUrl::fromLocalFile(path))`
  - `stock:<id>` → `engine.stock = id`
  - `control:<key>=<value>` → `engine.setFilmControl(key, value)` (number if it parses, `true`/`false` as bool, else string)
  - `expect:<target>=<value>` and `waitfor:<target>=<value>[,<timeoutMs>]` (default 15000)
  - `shot:<png path>`, `quit`
  - `<target>` = `<root>.<path>`: `<root>` is a context property (`engine`, `library`, `editStore`) or a QML `objectName`; `<path>` walks QObject properties, then QVariantMap keys; `.length` gives a list's size.
  - Failures log `UISCRIPT FAIL <step> (got '<actual>')`; `quit` exits with code 3 if any step failed, else 0.

- [ ] **Step 1: Create `desktop/src/UiScript.h`**

```cpp
#pragma once

#include <QString>

class QQmlApplicationEngine;

// Dev/test hook: drives the window from inside the process by posting synthetic
// events and calling the context objects -- never system-wide input. Run with
// QT_QPA_PLATFORM=offscreen. See desktop/tests/run_ui.ps1 for the step syntax.
void startUiScript(QQmlApplicationEngine& engine, const QString& spec);
```

- [ ] **Step 2: Create `desktop/src/UiScript.cpp`**

```cpp
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

QObject* resolveRoot(const ScriptState& s, const QString& name)
{
    const QVariant ctx = s.engine->rootContext()->contextProperty(name);
    if (auto* obj = ctx.value<QObject*>()) return obj;
    for (QObject* o : s.window->findChildren<QObject*>()) {
        if (o->objectName() == name) return o;
    }
    if (s.window->contentItem()) {
        if (auto* item = s.window->contentItem()->findChild<QQuickItem*>(name)) return item;
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
        if (key == QLatin1String("length") && value.canConvert<QVariantList>()) {
            value = value.toList().size();
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

void postClick(ScriptState& s, const QString& spec)
{
    const QString name = spec.section('@', 0, 0);
    const QString frac = spec.section('@', 1, 1);
    auto* item = qobject_cast<QQuickItem*>(resolveRoot(s, name));
    if (!item) { fail(s, "click:" + spec, "missing"); return; }
    const double fx = frac.isEmpty() ? 0.5 : frac.section(',', 0, 0).toDouble();
    const double fy = frac.isEmpty() ? 0.5 : frac.section(',', 1, 1).toDouble();
    const QPointF p = item->mapToScene(QPointF(item->width() * fx, item->height() * fy));
    const QPointF g = s.window->mapToGlobal(p);
    QCoreApplication::postEvent(s.window, new QMouseEvent(QEvent::MouseButtonPress, p, p, g,
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    QCoreApplication::postEvent(s.window, new QMouseEvent(QEvent::MouseButtonRelease, p, p, g,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
}

void postKey(ScriptState& s, const QString& spec)
{
    const int key = spec.section('+', 0, 0).toInt();
    const Qt::KeyboardModifiers mods = spec.contains("+ctrl") ? Qt::ControlModifier : Qt::NoModifier;
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
```

- [ ] **Step 3: Replace the inline runner in `desktop/src/main.cpp`**

Delete the `// Dev hook: DFEE_UI_SCRIPT ...` comment and the whole `static void runUiScript(...)` function, and the three includes `<QKeyEvent>`, `<QMouseEvent>`, `<QQuickItem>`. Add `#include "UiScript.h"` after `#include "LibraryController.h"`. Replace the call site:

```cpp
    if (qEnvironmentVariableIsSet("DFEE_UI_SCRIPT")) {
        startUiScript(engine, qEnvironmentVariable("DFEE_UI_SCRIPT"));
    }
```

- [ ] **Step 4: Add sources to `desktop/CMakeLists.txt`** (in `qt_add_executable(DFEE ...)`, after `src/LibraryController.h`):

```cmake
    src/UiScript.cpp
    src/UiScript.h
```

- [ ] **Step 5: Create the runner `desktop/tests/run_ui.ps1`**

```powershell
# Runs a UI script against the Release build, offscreen, with a throwaway catalog.
# Usage: powershell -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/smoke.script [-AppArgs @('--lightroom-edit','x.tif')]
# Scripts may use ${SAMPLE_A}, ${SAMPLE_B}, ${SAMPLE_TIFF}, ${TEMP} placeholders.
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string[]]$AppArgs = @(),
    [string]$SampleA = "E:/new_raws/3071874357.arw",
    [string]$SampleB = "E:/new_raws/7033866904.arw",
    [string]$SampleTiff = "E:/new_raws/3071874357.tif",
    [int]$TimeoutSec = 120,
    [string]$Exe = ""
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$exe = if ($Exe) { $Exe } else { Join-Path $repo "desktop\out\build\Release\DFEE.exe" }
$work = Join-Path ([IO.Path]::GetTempPath()) ("filmlab-ui-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
$tempTiff = Join-Path $work "lr-working.tif"
if (Test-Path $SampleTiff) { Copy-Item $SampleTiff $tempTiff }
$body = (Get-Content $Script -Raw).Replace('${SAMPLE_A}', $SampleA).Replace('${SAMPLE_B}', $SampleB).
    Replace('${SAMPLE_TIFF}', ($tempTiff -replace '\\', '/')).Replace('${TEMP}', ($work -replace '\\', '/'))
$scriptFile = Join-Path $work "script.txt"
[IO.File]::WriteAllText($scriptFile, $body)
$resolvedArgs = $AppArgs | ForEach-Object { $_.Replace('${SAMPLE_TIFF}', $tempTiff) }

$env:QT_QPA_PLATFORM = "offscreen"
$env:DFEE_UI_SCRIPT = "@" + $scriptFile
$env:DFEE_CATALOG_PATH = Join-Path $work "catalog.sqlite"
$env:PATH = (Join-Path $repo "cpp_engine\out\build\windows-msvc-vcpkg\vcpkg_installed\x64-windows\bin") + ";" + $env:PATH
# Start-Process rejects an empty -ArgumentList, so only pass it when there are arguments.
$proc = if ($resolvedArgs) { Start-Process -FilePath $exe -ArgumentList $resolvedArgs -PassThru }
        else { Start-Process -FilePath $exe -PassThru }
if (-not $proc.WaitForExit($TimeoutSec * 1000)) { $proc.Kill(); Write-Host "TIMEOUT"; exit 4 }
$log = Get-ChildItem "$env:LOCALAPPDATA\Film Lab\Film Lab\Logs" | Sort-Object LastWriteTime | Select-Object -Last 1
Get-Content $log.FullName | Select-String "UISCRIPT" | ForEach-Object { $_.Line.Substring(11) }
Write-Host "exit=$($proc.ExitCode) work=$work"
exit $proc.ExitCode
```

- [ ] **Step 6: Create `desktop/tests/ui/smoke.script`**

```
# Opens a RAW, picks a stock and a control, checks the controller state.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
stock:portra_400
control:film_contrast=150
expect:engine.stock=portra_400
expect:engine.filmControls.film_contrast=150
expect:engine.filmControls.no_such_key=1
quit
```

The last `expect` is deliberately wrong so the harness is seen to fail.

- [ ] **Step 7: Build and run — expect exactly one failure**

Run: `cmake --build desktop/out/build --config Release` then
`powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/smoke.script`
Expected: log shows `UISCRIPT FAIL expect:engine.filmControls.no_such_key=1 (got '<missing>')`, `UISCRIPT DONE failures=1`, `exit=3`.

- [ ] **Step 8: Remove the deliberate failure and re-run**

Delete the line `expect:engine.filmControls.no_such_key=1` from `smoke.script`. Re-run the command from Step 7.
Expected: `UISCRIPT DONE failures=0`, `exit=0`.

- [ ] **Step 9: Commit**

```bash
git add desktop/src/UiScript.h desktop/src/UiScript.cpp desktop/src/main.cpp desktop/CMakeLists.txt desktop/tests/run_ui.ps1 desktop/tests/ui/smoke.script
git commit -m "test(desktop): scriptable offscreen UI harness (open/control/expect/waitfor/shot/quit)"
```

---

### Task 2: EditStore (SQLite per-photo memory) with unit tests

**Files:**
- Create: `desktop/src/EditStore.h`, `desktop/src/EditStore.cpp`
- Create: `desktop/tests/edit_store_test.cpp`
- Modify: `desktop/CMakeLists.txt` (Qt Sql/Test, sources, test target)

**Interfaces:**
- Produces:
  ```cpp
  struct StoredHistoryStep { QString label; QString coalesceKey; QString stock; QVariantMap controls; };
  struct EditRecord { QString stock; QVariantMap controls; QVector<StoredHistoryStep> history; int historyIndex = -1; };
  class EditStore : public QObject {
      Q_PROPERTY(int recordCount READ recordCount NOTIFY changed)
      explicit EditStore(const QString& databasePath, QObject* parent = nullptr);
      static QString defaultPath();              // DFEE_CATALOG_PATH or AppDataLocation/catalog.sqlite
      static QString pathKey(const QString& path);
      static constexpr int kMaxHistory = 50;
      bool isOpen() const;
      std::optional<EditRecord> load(const QString& photoPath) const;
      bool save(const QString& photoPath, const EditRecord& record, bool edited);
      int recordCount() const;
  signals: void changed();
  };
  ```
  `controls` maps are stored as given (callers pass sparse maps); JSON numbers come back as `double`.

- [ ] **Step 1: Add Qt modules and the test target to `desktop/CMakeLists.txt`**

Change the `find_package` line to:

```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Gui Concurrent QuickControls2 Sql Test)
```

Add to `qt_add_executable(DFEE ...)` sources:

```cmake
    src/EditStore.cpp
    src/EditStore.h
```

Change the link line to:

```cmake
target_link_libraries(DFEE PRIVATE Qt6::Quick Qt6::Gui Qt6::Concurrent Qt6::QuickControls2 Qt6::Sql dfee_core)
```

Append at the end of the file:

```cmake
# Desktop unit tests (Qt Test). Pure desktop logic only -- no engine session.
enable_testing()
qt_add_executable(desktop_tests
    tests/edit_store_test.cpp
    src/EditStore.cpp
    src/EditStore.h
)
target_include_directories(desktop_tests PRIVATE src)
target_link_libraries(desktop_tests PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
target_compile_definitions(desktop_tests PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME desktop_tests COMMAND desktop_tests)
```

- [ ] **Step 2: Write the failing tests `desktop/tests/edit_store_test.cpp`**

```cpp
#include "EditStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class EditStoreTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripsRecordAndHistory()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        QVERIFY(store.isOpen());
        EditRecord in;
        in.stock = "portra_400";
        in.controls = {{"film_contrast", 150.0}, {"flip_h", true}};
        in.history = {{"Import", "", "none", {}},
                      {"Film stock: Kodak Portra 400", "stock", "portra_400", {}},
                      {"Film contrast", "film_contrast", "portra_400", {{"film_contrast", 150.0}}}};
        in.historyIndex = 2;
        QVERIFY(store.save("E:/shoot/A.ARW", in, true));
        const auto out = store.load("E:/shoot/A.ARW");
        QVERIFY(out.has_value());
        QCOMPARE(out->stock, QString("portra_400"));
        QCOMPARE(out->controls.value("film_contrast").toDouble(), 150.0);
        QCOMPARE(out->controls.value("flip_h").toBool(), true);
        QCOMPARE(out->history.size(), 3);
        QCOMPARE(out->history.at(1).coalesceKey, QString("stock"));
        QCOMPARE(out->history.at(2).controls.value("film_contrast").toDouble(), 150.0);
        QCOMPARE(out->historyIndex, 2);
        QCOMPARE(store.recordCount(), 1);
    }

    void missingPhotoLoadsNothing()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        QVERIFY(!store.load("E:/never/opened.nef").has_value());
    }

    void pathKeyNormalises()
    {
        QCOMPARE(EditStore::pathKey("E:\\Shoot\\A.ARW"), EditStore::pathKey("e:/shoot/a.arw"));
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "velvia_50"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
        QVERIFY(store.save("E:\\Shoot\\A.ARW", r, true));
        const auto out = store.load("e:/shoot/a.arw");
        QVERIFY(out.has_value());
        QCOMPARE(out->stock, QString("velvia_50"));
    }

    void uneditedPhotoIsNotInserted()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "none"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/B.ARW", r, false));
        QCOMPARE(store.recordCount(), 0);
    }

    void resetPhotoUpdatesExistingRow()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord edited; edited.stock = "tri_x_400"; edited.history = {{"Import", "", "none", {}}}; edited.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/C.ARW", edited, true));
        EditRecord reset; reset.stock = "none"; reset.history = edited.history; reset.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/C.ARW", reset, false));
        QCOMPARE(store.load("E:/shoot/C.ARW")->stock, QString("none"));
        QCOMPARE(store.recordCount(), 1);
    }

    void historyIsCappedKeepingBaseline()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "gold_200";
        for (int i = 0; i < 80; ++i) r.history.append({QString("step %1").arg(i), "", "gold_200", {}});
        r.historyIndex = 79;
        QVERIFY(store.save("E:/shoot/D.ARW", r, true));
        const auto out = store.load("E:/shoot/D.ARW");
        QCOMPARE(out->history.size(), EditStore::kMaxHistory);
        QCOMPARE(out->history.first().label, QString("step 0"));
        QCOMPARE(out->history.last().label, QString("step 79"));
        QCOMPARE(out->historyIndex, EditStore::kMaxHistory - 1);
    }

    void unopenableStoreIsHarmless()
    {
        QTemporaryDir dir;
        // A directory where the database file should be: SQLite cannot open it.
        QDir(dir.path()).mkdir("catalog.sqlite");
        EditStore store(dir.filePath("catalog.sqlite"));
        QVERIFY(!store.isOpen());
        EditRecord r; r.stock = "portra_160";
        QVERIFY(!store.save("E:/shoot/E.ARW", r, true));
        QVERIFY(!store.load("E:/shoot/E.ARW").has_value());
        QCOMPARE(store.recordCount(), 0);
    }

    void survivesReopen()
    {
        QTemporaryDir dir;
        {
            EditStore store(dir.filePath("catalog.sqlite"));
            EditRecord r; r.stock = "ektar_100"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
            QVERIFY(store.save("E:/shoot/F.ARW", r, true));
        }
        EditStore again(dir.filePath("catalog.sqlite"));
        QCOMPARE(again.load("E:/shoot/F.ARW")->stock, QString("ektar_100"));
    }
};

QTEST_GUILESS_MAIN(EditStoreTest)
#include "edit_store_test.moc"
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build desktop/out/build --config Release --target desktop_tests`
Expected: compile error — `EditStore.h` not found.

- [ ] **Step 4: Create `desktop/src/EditStore.h`**

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

// One history step as persisted: label, coalescing key, stock and (sparse) controls.
struct StoredHistoryStep {
    QString label;
    QString coalesceKey;
    QString stock;
    QVariantMap controls;
};

// A photo's persisted edit state. `controls` is whatever map the caller saves
// (EngineController stores only non-default keys); numbers return as double.
struct EditRecord {
    QString stock;
    QVariantMap controls;
    QVector<StoredHistoryStep> history;
    int historyIndex = -1;
};

// Per-photo edit memory: a SQLite catalog in the app's data folder, keyed by a
// normalised path. Photo folders are never written to. If the database cannot be
// opened the store stays closed and every call is a harmless no-op.
class EditStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int recordCount READ recordCount NOTIFY changed)
public:
    static constexpr int kMaxHistory = 50;

    explicit EditStore(const QString& databasePath, QObject* parent = nullptr);
    ~EditStore() override;

    // DFEE_CATALOG_PATH when set (tests), else <AppDataLocation>/catalog.sqlite.
    static QString defaultPath();
    // Absolute, cleaned, forward slashes, lower-case (Windows paths are case-insensitive).
    static QString pathKey(const QString& path);

    bool isOpen() const { return open_; }
    std::optional<EditRecord> load(const QString& photoPath) const;
    // Writes the record. An unedited photo with no existing row is not inserted,
    // so merely viewing photos never grows the catalog.
    bool save(const QString& photoPath, const EditRecord& record, bool edited);
    int recordCount() const;

signals:
    void changed();

private:
    QString connection_;
    bool open_ = false;
};
```

- [ ] **Step 5: Create `desktop/src/EditStore.cpp`**

```cpp
#include "EditStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QDebug>

namespace {
constexpr int kSchemaVersion = 1;

QByteArray historyToJson(const QVector<StoredHistoryStep>& steps)
{
    QJsonArray arr;
    for (const StoredHistoryStep& s : steps) {
        QJsonObject o;
        o["label"] = s.label;
        o["key"] = s.coalesceKey;
        o["stock"] = s.stock;
        o["controls"] = QJsonObject::fromVariantMap(s.controls);
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QVector<StoredHistoryStep> historyFromJson(const QByteArray& json)
{
    QVector<StoredHistoryStep> steps;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        steps.append({o.value("label").toString(), o.value("key").toString(),
                      o.value("stock").toString(), o.value("controls").toObject().toVariantMap()});
    }
    return steps;
}

// Keep the baseline (step 0) and the newest kMaxHistory-1 steps.
void capHistory(QVector<StoredHistoryStep>& steps, int& index)
{
    const int excess = int(steps.size()) - EditStore::kMaxHistory;
    if (excess <= 0) return;
    steps.erase(steps.begin() + 1, steps.begin() + 1 + excess);
    index = index <= 0 ? 0 : std::max(1, index - excess);
}
}  // namespace

EditStore::EditStore(const QString& databasePath, QObject* parent)
    : QObject(parent)
    , connection_(QStringLiteral("filmlab-catalog-") + QUuid::createUuid().toString(QUuid::Id128))
{
    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_);
    db.setDatabaseName(databasePath);
    if (!db.open()) {
        qWarning() << "EditStore: cannot open" << databasePath << db.lastError().text();
        return;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    const bool created = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS photos ("
        " path_key TEXT PRIMARY KEY, path TEXT NOT NULL, stock TEXT NOT NULL,"
        " controls_json TEXT NOT NULL, history_json TEXT NOT NULL, history_index INTEGER NOT NULL,"
        " edited INTEGER NOT NULL, schema_version INTEGER NOT NULL, updated_at TEXT NOT NULL)"));
    if (!created) {
        qWarning() << "EditStore: cannot create schema" << q.lastError().text();
        return;
    }
    open_ = true;
}

EditStore::~EditStore()
{
    {
        QSqlDatabase db = QSqlDatabase::database(connection_, false);
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(connection_);
}

QString EditStore::defaultPath()
{
    const QString overridePath = qEnvironmentVariable("DFEE_CATALOG_PATH");
    if (!overridePath.isEmpty()) return overridePath;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/catalog.sqlite");
}

QString EditStore::pathKey(const QString& path)
{
    QString p = path;
    p.replace('\\', '/');
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath()).toLower();
}

std::optional<EditRecord> EditStore::load(const QString& photoPath) const
{
    if (!open_) return std::nullopt;
    QSqlQuery q(QSqlDatabase::database(connection_));
    q.prepare(QStringLiteral(
        "SELECT stock, controls_json, history_json, history_index FROM photos WHERE path_key = ?"));
    q.addBindValue(pathKey(photoPath));
    if (!q.exec() || !q.next()) return std::nullopt;
    EditRecord r;
    r.stock = q.value(0).toString();
    r.controls = QJsonDocument::fromJson(q.value(1).toByteArray()).object().toVariantMap();
    r.history = historyFromJson(q.value(2).toByteArray());
    r.historyIndex = q.value(3).toInt();
    return r;
}

bool EditStore::save(const QString& photoPath, const EditRecord& record, bool edited)
{
    if (!open_) return false;
    QSqlDatabase db = QSqlDatabase::database(connection_);
    const QString key = pathKey(photoPath);
    if (!edited) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral("SELECT 1 FROM photos WHERE path_key = ?"));
        exists.addBindValue(key);
        if (exists.exec() && !exists.next()) return true;  // never edited: nothing to remember
    }
    QVector<StoredHistoryStep> history = record.history;
    int index = record.historyIndex;
    capHistory(history, index);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO photos (path_key, path, stock, controls_json, history_json, history_index,"
        " edited, schema_version, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(path_key) DO UPDATE SET path = excluded.path, stock = excluded.stock,"
        " controls_json = excluded.controls_json, history_json = excluded.history_json,"
        " history_index = excluded.history_index, edited = excluded.edited,"
        " schema_version = excluded.schema_version, updated_at = excluded.updated_at"));
    q.addBindValue(key);
    q.addBindValue(photoPath);
    q.addBindValue(record.stock);
    q.addBindValue(QJsonDocument(QJsonObject::fromVariantMap(record.controls)).toJson(QJsonDocument::Compact));
    q.addBindValue(historyToJson(history));
    q.addBindValue(index);
    q.addBindValue(edited ? 1 : 0);
    q.addBindValue(kSchemaVersion);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "EditStore: save failed" << q.lastError().text();
        return false;
    }
    emit changed();
    return true;
}

int EditStore::recordCount() const
{
    if (!open_) return 0;
    QSqlQuery q(QSqlDatabase::database(connection_));
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM photos")) || !q.next()) return 0;
    return q.value(0).toInt();
}
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build desktop/out/build --config Release --target desktop_tests` then
`desktop/out/build/Release/desktop_tests.exe` (with `C:\Qt\6.8.3\msvc2022_64\bin` on PATH).
Expected: `Totals: 9 passed, 0 failed` (7 tests + initTestCase/cleanupTestCase).

- [ ] **Step 7: Commit**

```bash
git add desktop/src/EditStore.h desktop/src/EditStore.cpp desktop/tests/edit_store_test.cpp desktop/CMakeLists.txt
git commit -m "feat(desktop): SQLite EditStore for per-photo edit memory"
```

---

### Task 3: Wire per-photo memory into EngineController

**Files:**
- Modify: `desktop/src/EngineController.h`, `desktop/src/EngineController.cpp`
- Modify: `desktop/src/main.cpp` (create the store, pass it, expose `editStore`)
- Create: `desktop/tests/ui/memory.script`, `desktop/tests/ui/lightroom_bypass.script`

**Interfaces:**
- Consumes: `EditStore`, `EditRecord`, `StoredHistoryStep` (Task 2); `startUiScript` (Task 1).
- Produces: `EngineController(PreviewImageProvider* provider, EditStore* store, QObject* parent = nullptr)`; `Q_INVOKABLE void flushEdits()`; context property `editStore`. Behaviour: a photo's stock/controls/history are restored on open; unknown photos open clean.

- [ ] **Step 1: Write the failing UI script `desktop/tests/ui/memory.script`**

```
# Edit A, open B at once (queued behind A's render): B must open clean.
# Return to A: its look and history must come back.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
stock:portra_400
control:film_contrast=150
open:${SAMPLE_B}
waitfor:engine.currentFile=${SAMPLE_B},15000
waitfor:engine.stock=none,15000
expect:engine.filmControls.film_contrast=100
waitfor:engine.history.length=1,15000
open:${SAMPLE_A}
waitfor:engine.stock=portra_400,15000
expect:engine.filmControls.film_contrast=150
expect:engine.history.length=3
expect:editStore.recordCount=1
quit
```

- [ ] **Step 2: Run it to verify it fails**

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/memory.script`
Expected: FAIL on `waitfor:engine.stock=none` (got `portra_400` — today's carry-over) and on `expect:editStore.recordCount=1` (got `<missing>`); `exit=3`.

- [ ] **Step 3: Header changes in `desktop/src/EngineController.h`**

Add `#include "EditStore.h"` after `#include "dfee/bridge_types.hpp"`. Add the property after the `currentFile` property:

```cpp
    // Camera / capture details of the open photo (camera, lens, iso, shutter, aperture,
    // focal, width, height, inputKind, developerProfile); empty until decoded.
    Q_PROPERTY(QVariantMap imageInfo READ imageInfo NOTIFY imageInfoChanged)
```

Replace the constructor declaration with:

```cpp
    explicit EngineController(PreviewImageProvider* provider,
                              EditStore* store,
                              QObject* parent = nullptr);
```

Add public members (next to `currentFile()`):

```cpp
    QVariantMap imageInfo() const { return imageInfo_; }
    // Persist the current photo's edits now (photo switch, export, quit).
    Q_INVOKABLE void flushEdits();
    Q_INVOKABLE void onImageInfo(const QVariantMap& info);
```

Add the signal `void imageInfoChanged();` after `void currentFileChanged();`. Add private declarations after `restoreHistory`:

```cpp
    // Per-photo memory. loadEditsFor replaces the whole edit state (stock, controls,
    // history) with the photo's stored record, or with defaults if it has none.
    void loadEditsFor(const QString& file);
    void saveEditsFor(const QString& file);
    void markEditsDirty();
    bool isEdited() const;
    static QVariantMap sparseControls(const QVariantMap& controls);
    static QVariantMap mergeOnDefaults(const QVariantMap& sparse);
    static bool sameControls(const QVariantMap& a, const QVariantMap& b);
```

Add private members after `bool lightroomRoundTrip_ = false;`:

```cpp
    EditStore* store_ = nullptr;          // not owned; null disables memory
    QTimer saveTimer_;                    // debounced store write after an edit
    QString grainRequestFile_;            // photo an Auto-grain request was made for
    QVariantMap imageInfo_;
```

- [ ] **Step 4: Constructor in `desktop/src/EngineController.cpp`**

Replace the constructor signature and initializer with:

```cpp
EngineController::EngineController(PreviewImageProvider* provider,
                                   EditStore* store,
                                   QObject* parent)
    : QObject(parent)
    , session_(std::make_unique<dfee::EngineSession>(resolveProjectRoot()))
    , provider_(provider)
    , store_(store)
{
    filmControls_ = defaultFilmControls();
    previewDebounceTimer_.setSingleShot(true);
    previewDebounceTimer_.setInterval(90);
    connect(&previewDebounceTimer_, &QTimer::timeout,
            this, &EngineController::dispatchScheduledRender);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(400);
    connect(&saveTimer_, &QTimer::timeout, this, [this]() { saveEditsFor(currentFile_); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &EngineController::flushEdits);
```

(the rest of the constructor body is unchanged).

- [ ] **Step 5: Load/save helpers** — add after `restoreHistory` in `EngineController.cpp`:

```cpp
// ── Per-photo memory ────────────────────────────────────────────────────

QVariantMap EngineController::sparseControls(const QVariantMap& controls)
{
    const QVariantMap defaults = defaultFilmControls();
    QVariantMap sparse;
    for (auto it = controls.cbegin(); it != controls.cend(); ++it) {
        if (!defaults.contains(it.key())) continue;
        if (!sameControls({{it.key(), it.value()}}, {{it.key(), defaults.value(it.key())}})) {
            sparse.insert(it.key(), it.value());
        }
    }
    return sparse;
}

QVariantMap EngineController::mergeOnDefaults(const QVariantMap& sparse)
{
    QVariantMap controls = defaultFilmControls();
    for (auto it = sparse.cbegin(); it != sparse.cend(); ++it) {
        if (controls.contains(it.key())) controls.insert(it.key(), it.value());  // drop unknown keys
    }
    return controls;
}

// Value-wise equality that treats 150 and 150.0 as equal (JSON returns doubles).
bool EngineController::sameControls(const QVariantMap& a, const QVariantMap& b)
{
    if (a.size() != b.size()) return false;
    for (auto it = a.cbegin(); it != a.cend(); ++it) {
        const auto other = b.constFind(it.key());
        if (other == b.cend()) return false;
        const QVariant& x = it.value();
        const QVariant& y = other.value();
        bool okX = false, okY = false;
        const double dx = x.toDouble(&okX);
        const double dy = y.toDouble(&okY);
        const bool numeric = okX && okY && x.typeId() != QMetaType::QString && y.typeId() != QMetaType::QString;
        if (numeric ? !qFuzzyCompare(dx + 1.0, dy + 1.0) : x.toString() != y.toString()) return false;
    }
    return true;
}

bool EngineController::isEdited() const
{
    return stockId_ != QStringLiteral("none") || !sameControls(filmControls_, defaultFilmControls());
}

void EngineController::loadEditsFor(const QString& file)
{
    saveTimer_.stop();
    filmControls_ = defaultFilmControls();
    QString stock = QStringLiteral("none");
    history_.clear();
    historyIndex_ = -1;
    pendingSeed_ = true;                  // seed "Import" on first preview unless restored
    if (store_ && !lightroomRoundTrip_) {
        if (const auto rec = store_->load(file)) {
            filmControls_ = mergeOnDefaults(rec->controls);
            stock = stockIds_.contains(rec->stock) ? rec->stock : QStringLiteral("none");
            for (const StoredHistoryStep& s : rec->history) {
                history_.append(HistoryEntry{s.label, s.coalesceKey,
                    stockIds_.contains(s.stock) ? s.stock : QStringLiteral("none"),
                    mergeOnDefaults(s.controls)});
            }
            if (!history_.isEmpty()) {
                historyIndex_ = std::clamp(rec->historyIndex, 0, int(history_.size()) - 1);
                pendingSeed_ = false;
            }
        }
    }
    filmExposure_ = filmControls_.value("film_exposure_ev").toDouble();
    shadowLift_ = filmControls_.value("shadow_lift").toDouble();
    if (stockId_ != stock) {
        stockId_ = stock;
        emit stockChanged();
    }
    if (grainResolving_) {
        grainResolving_ = false;
        emit grainResolvingChanged();
    }
    emit filmControlsChanged();
    emit paramsChanged();
    emit historyChanged();
}

void EngineController::saveEditsFor(const QString& file)
{
    if (!store_ || lightroomRoundTrip_ || file.isEmpty() || history_.isEmpty()) return;
    EditRecord rec;
    rec.stock = stockId_;
    rec.controls = sparseControls(filmControls_);
    for (const HistoryEntry& e : history_) {
        rec.history.append({e.label, e.coalesceKey, e.stock, sparseControls(e.controls)});
    }
    rec.historyIndex = historyIndex_;
    store_->save(file, rec, isEdited());
}

void EngineController::markEditsDirty()
{
    if (store_ && !lightroomRoundTrip_) saveTimer_.start();
}

void EngineController::flushEdits()
{
    saveTimer_.stop();
    saveEditsFor(currentFile_);
}

void EngineController::onImageInfo(const QVariantMap& info)
{
    imageInfo_ = info;
    emit imageInfoChanged();
}
```

- [ ] **Step 6: Mark dirty after every history change**

At the end of `EngineController::recordHistory` (after `emit historyChanged();`) and at the end of `EngineController::restoreHistory` (after `scheduleRender();`) add:

```cpp
    markEditsDirty();
```

- [ ] **Step 7: Load on open (both open paths), flush on switch**

In `openFile`, replace:

```cpp
    currentFile_ = file;
    emit currentFileChanged();
    workerBusy_  = true;
```

with:

```cpp
    flushEdits();                         // persist the outgoing photo first
    currentFile_ = file;
    emit currentFileChanged();
    imageInfo_.clear();
    emit imageInfoChanged();
    loadEditsFor(file);                   // before the first preview request
    workerBusy_  = true;
```

In `onWorkerBusyChanged`, replace:

```cpp
            dirtyIsOpen_  = false;
            currentFile_  = pendingFile_;
            pendingFile_.clear();
            emit currentFileChanged();
```

with:

```cpp
            dirtyIsOpen_  = false;
            flushEdits();                 // the outgoing photo's state is still loaded
            currentFile_  = pendingFile_;
            pendingFile_.clear();
            emit currentFileChanged();
            imageInfo_.clear();
            emit imageInfoChanged();
            loadEditsFor(currentFile_);
```

In `onOpenFailed`, inside `if (!dirtyIsOpen_) {` after `emit currentFileChanged();` add:

```cpp
        imageInfo_.clear();
        emit imageInfoChanged();
```

- [ ] **Step 8: Flush before export; guard Auto grain**

At the top of `exportImage()` after `if (exporting_) return;` add `flushEdits();`.
In `setAutoGrain`, just before `grainResolving_ = true;` add `grainRequestFile_ = currentFile_;`.
At the top of `onAutoGrainResolved`, after `emit grainResolvingChanged();` add:

```cpp
    if (grainRequestFile_ != currentFile_) return;  // the photo changed while resolving
```

- [ ] **Step 9: Create the store in `desktop/src/main.cpp`**

Add `#include "EditStore.h"`. Replace `EngineController controller(provider);` with:

```cpp
    // Per-photo edit memory (a SQLite catalog in the app data folder, or
    // DFEE_CATALOG_PATH for tests). A store that cannot open disables memory only.
    EditStore editStore(EditStore::defaultPath());
    EngineController controller(provider, &editStore);
```

After `engine.rootContext()->setContextProperty("library", &library);` add:

```cpp
    engine.rootContext()->setContextProperty("editStore", &editStore);
```

- [ ] **Step 10: Build and run the memory script**

Run: `cmake --build desktop/out/build --config Release` then
`powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/memory.script`
Expected: `UISCRIPT DONE failures=0`, `exit=0`.

- [ ] **Step 11: Lightroom bypass script `desktop/tests/ui/lightroom_bypass.script`**

```
# Lightroom round-trip: edits must never reach the catalog.
waitfor:engine.hasImage=true,30000
stock:portra_400
control:film_contrast=150
wait:1200
expect:engine.lightroomRoundTrip=true
expect:editStore.recordCount=0
quit
```

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/lightroom_bypass.script -AppArgs @('--lightroom-edit','${SAMPLE_TIFF}')`
Expected: `failures=0`, `exit=0` (the runner edits a temp copy of the TIFF; nothing is exported).

- [ ] **Step 12: Re-run the smoke script** (regression)

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/smoke.script`
Expected: `failures=0`.

- [ ] **Step 13: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/src/main.cpp desktop/tests/ui/memory.script desktop/tests/ui/lightroom_bypass.script
git commit -m "feat(desktop): remember each photo's look and history; new photos open clean"
```

---

### Task 4: Image metadata to QML (`engine.imageInfo`)

**Files:**
- Create: `desktop/src/ImageInfo.h`, `desktop/src/ImageInfo.cpp`
- Modify: `desktop/src/RenderWorker.cpp` (post metadata after a good decode)
- Modify: `desktop/CMakeLists.txt` (sources in app and test target)
- Modify: `desktop/tests/edit_store_test.cpp` → add a second test class in `desktop/tests/image_info_test.cpp`
- Create: `desktop/tests/ui/info.script`

**Interfaces:**
- Consumes: `EngineController::onImageInfo(QVariantMap)` (Task 3).
- Produces: `QVariantMap imageInfoFromMetadata(const dfee::NativeRawMetadata& md);` keys: `camera`, `lens`, `iso` (int), `shutter`, `aperture` (e.g. `"f/2.8"`), `focal` (e.g. `"35 mm"`), `width`, `height`, `inputKind`, `developerProfile`; keys whose value is empty/zero are omitted; rendered TIFFs (`input_kind == "rendered"`) carry no camera fields.

- [ ] **Step 1: Write the failing test `desktop/tests/image_info_test.cpp`**

```cpp
#include "ImageInfo.h"

#include <QtTest>

class ImageInfoTest : public QObject {
    Q_OBJECT
private slots:
    void rawCaptureDetails()
    {
        dfee::NativeRawMetadata md;
        md.camera_make = "Sony";
        md.camera_model = "ILCE-7CR";
        md.lens_model = "FE 35mm F1.8";
        md.iso = 400;
        md.shutter_speed_str = "1/250";
        md.aperture = 2.8;
        md.focal_length = 35.0;
        md.image_width = 9504;
        md.image_height = 6336;
        md.input_kind = "developed_raw";
        md.developer_profile = "Sony ILCE-7CR Adobe Standard + Adobe Color";
        const QVariantMap m = imageInfoFromMetadata(md);
        QCOMPARE(m.value("camera").toString(), QString("Sony ILCE-7CR"));
        QCOMPARE(m.value("lens").toString(), QString("FE 35mm F1.8"));
        QCOMPARE(m.value("iso").toInt(), 400);
        QCOMPARE(m.value("shutter").toString(), QString("1/250"));
        QCOMPARE(m.value("aperture").toString(), QString("f/2.8"));
        QCOMPARE(m.value("focal").toString(), QString("35 mm"));
        QCOMPARE(m.value("width").toInt(), 9504);
        QCOMPARE(m.value("inputKind").toString(), QString("developed_raw"));
    }

    void modelAlreadyNamesMaker()
    {
        dfee::NativeRawMetadata md;
        md.camera_make = "Canon";
        md.camera_model = "Canon EOS R";
        md.input_kind = "developed_raw";
        QCOMPARE(imageInfoFromMetadata(md).value("camera").toString(), QString("Canon EOS R"));
    }

    void wholeApertureHasNoDecimal()
    {
        dfee::NativeRawMetadata md;
        md.aperture = 2.0;
        md.input_kind = "developed_raw";
        QCOMPARE(imageInfoFromMetadata(md).value("aperture").toString(), QString("f/2"));
    }

    void renderedTiffHasNoCameraFields()
    {
        dfee::NativeRawMetadata md;
        md.camera_make = "Rendered";
        md.camera_model = "TIFF";
        md.iso = 0;
        md.image_width = 4000;
        md.image_height = 3000;
        md.input_kind = "rendered";
        const QVariantMap m = imageInfoFromMetadata(md);
        QVERIFY(!m.contains("camera"));
        QVERIFY(!m.contains("iso"));
        QVERIFY(!m.contains("aperture"));
        QCOMPARE(m.value("width").toInt(), 4000);
        QCOMPARE(m.value("inputKind").toString(), QString("rendered"));
    }
};

QTEST_GUILESS_MAIN(ImageInfoTest)
#include "image_info_test.moc"
```

- [ ] **Step 2: Add a second test executable to `desktop/CMakeLists.txt`** (append):

```cmake
qt_add_executable(image_info_tests
    tests/image_info_test.cpp
    src/ImageInfo.cpp
    src/ImageInfo.h
)
target_include_directories(image_info_tests PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../cpp_engine/include)
target_link_libraries(image_info_tests PRIVATE Qt6::Core Qt6::Test)
add_test(NAME image_info_tests COMMAND image_info_tests)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build desktop/out/build --config Release --target image_info_tests`
Expected: compile error — `ImageInfo.h` not found.

- [ ] **Step 4: Create `desktop/src/ImageInfo.h`**

```cpp
#pragma once

#include <QVariantMap>

#include "dfee/bridge_types.hpp"

// Capture details for the UI from the engine's decode metadata. Empty or zero
// values are omitted; rendered (TIFF) inputs carry no camera fields because the
// engine does not read their EXIF.
QVariantMap imageInfoFromMetadata(const dfee::NativeRawMetadata& md);
```

- [ ] **Step 5: Create `desktop/src/ImageInfo.cpp`**

```cpp
#include "ImageInfo.h"

#include <QString>

#include <cmath>

QVariantMap imageInfoFromMetadata(const dfee::NativeRawMetadata& md)
{
    QVariantMap m;
    const QString kind = QString::fromStdString(md.input_kind);
    m["inputKind"] = kind;
    if (md.image_width > 0) m["width"] = md.image_width;
    if (md.image_height > 0) m["height"] = md.image_height;
    if (!md.developer_profile.empty()) m["developerProfile"] = QString::fromStdString(md.developer_profile);
    if (kind == QLatin1String("rendered")) return m;

    const QString make = QString::fromStdString(md.camera_make).trimmed();
    const QString model = QString::fromStdString(md.camera_model).trimmed();
    const QString camera = model.startsWith(make, Qt::CaseInsensitive) ? model
                                                                      : (make + ' ' + model).trimmed();
    if (!camera.isEmpty()) m["camera"] = camera;
    if (!md.lens_model.empty()) m["lens"] = QString::fromStdString(md.lens_model);
    if (md.iso > 0) m["iso"] = md.iso;
    if (!md.shutter_speed_str.empty()) m["shutter"] = QString::fromStdString(md.shutter_speed_str);
    if (md.aperture > 0.0) m["aperture"] = QStringLiteral("f/") + QString::number(md.aperture, 'g', 3);
    if (md.focal_length > 0.0) m["focal"] = QString::number(std::lround(md.focal_length)) + QStringLiteral(" mm");
    return m;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build desktop/out/build --config Release --target image_info_tests` then `desktop/out/build/Release/image_info_tests.exe`
Expected: `Totals: 6 passed, 0 failed`.

- [ ] **Step 7: Post metadata from the worker**

Add `src/ImageInfo.cpp` and `src/ImageInfo.h` to `qt_add_executable(DFEE ...)`. In `desktop/src/RenderWorker.cpp` add `#include "ImageInfo.h"` and, in `openAndRender`, right after the `if (!decoded.ok) { ... return; }` block:

```cpp
        QMetaObject::invokeMethod(controller_, "onImageInfo", Qt::QueuedConnection,
                                  Q_ARG(QVariantMap, imageInfoFromMetadata(decoded.metadata)));
```

- [ ] **Step 8: UI script `desktop/tests/ui/info.script`**

```
open:${SAMPLE_A}
waitfor:engine.imageInfo.camera=Sony ILCE-7CR,30000
expect:engine.imageInfo.inputKind=developed_raw
quit
```

Run: `cmake --build desktop/out/build --config Release` then
`powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/info.script`
Expected: `failures=0`, `exit=0`.

- [ ] **Step 9: Commit**

```bash
git add desktop/src/ImageInfo.h desktop/src/ImageInfo.cpp desktop/src/RenderWorker.cpp desktop/CMakeLists.txt desktop/tests/image_info_test.cpp desktop/tests/ui/info.script
git commit -m "feat(desktop): expose the open photo's capture details as engine.imageInfo"
```

---

### Task 5: Stock catalog (group, ISO, one-line look) in the stock model

**Files:**
- Create: `desktop/resources/stocks/catalog.json`
- Create: `desktop/src/StockCatalog.h`, `desktop/src/StockCatalog.cpp`
- Create: `desktop/tests/stock_catalog_test.cpp`
- Modify: `desktop/src/EngineController.cpp` (`loadStocks`), `desktop/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct StockInfo { QString group; int iso = 0; QString blurb; };
  QHash<QString, StockInfo> loadStockCatalog(const QString& path);   // missing/invalid file -> empty
  QString stockGroupLabel(const QString& group);                     // negative|slide|bw -> label
  ```
  `engine.stockModel` rows gain `group` (`negative`/`slide`/`bw`), `groupLabel` (`Color negative`/`Slide`/`B&W`), `iso` (int) and `blurb`; existing `id/name/type/typeLabel` are unchanged.

- [ ] **Step 1: Create `desktop/resources/stocks/catalog.json`**

```json
{
  "schemaVersion": 1,
  "stocks": {
    "portra_160":        {"group": "negative", "iso": 160,  "blurb": "Fine grain, soft contrast, natural warm skin"},
    "portra_400":        {"group": "negative", "iso": 400,  "blurb": "Warm, forgiving skin with soft pastel highlights"},
    "portra_800":        {"group": "negative", "iso": 800,  "blurb": "Warmer, grainier Portra for low light"},
    "ektar_100":         {"group": "negative", "iso": 100,  "blurb": "Punchy saturation and deep blues, very fine grain"},
    "gold_200":          {"group": "negative", "iso": 200,  "blurb": "Golden, nostalgic consumer warmth"},
    "colorplus_200":     {"group": "negative", "iso": 200,  "blurb": "Warm yellows and gentle contrast"},
    "ultramax_400":      {"group": "negative", "iso": 400,  "blurb": "Bright everyday color with visible grain"},
    "vision3_250d":      {"group": "negative", "iso": 250,  "blurb": "Daylight cinema: wide latitude, gentle color"},
    "vision3_500t":      {"group": "negative", "iso": 500,  "blurb": "Tungsten cinema: cool shadows, glowing lights"},
    "cinestill_50d":     {"group": "negative", "iso": 50,   "blurb": "Clean daylight cine look with crisp color"},
    "cinestill_400d":    {"group": "negative", "iso": 400,  "blurb": "Versatile cine color with a soft warm glow"},
    "cinestill_800t":    {"group": "negative", "iso": 800,  "blurb": "Night film: red halos around lights, cool cast"},
    "superia_400":       {"group": "negative", "iso": 400,  "blurb": "Cool greens and crisp consumer Fuji color"},
    "fujicolor_c200":    {"group": "negative", "iso": 200,  "blurb": "Soft, slightly cool everyday Fuji look"},
    "pro_400h":          {"group": "negative", "iso": 400,  "blurb": "Airy pastels with mint greens"},
    "fuji_eterna_250d":  {"group": "negative", "iso": 250,  "blurb": "Muted, low-contrast cinema color"},
    "ektachrome_100":    {"group": "slide",    "iso": 100,  "blurb": "Clean, cool slide color with bright blues"},
    "kodachrome_64":     {"group": "slide",    "iso": 64,   "blurb": "Rich reds and deep contrast of the classic slide"},
    "velvia_50":         {"group": "slide",    "iso": 50,   "blurb": "Intense saturation and deep blacks"},
    "velvia_100":        {"group": "slide",    "iso": 100,  "blurb": "Vivid slide color, a touch gentler than Velvia 50"},
    "provia_100f":       {"group": "slide",    "iso": 100,  "blurb": "Neutral, accurate slide color"},
    "astia_100":         {"group": "slide",    "iso": 100,  "blurb": "Soft slide contrast, flattering skin"},
    "tri_x_400":         {"group": "bw",       "iso": 400,  "blurb": "Classic gritty black and white, punchy contrast"},
    "tmax_100":          {"group": "bw",       "iso": 100,  "blurb": "Very fine grain, smooth modern tones"},
    "tmax_400":          {"group": "bw",       "iso": 400,  "blurb": "Sharp, clean modern black and white"},
    "eastman_double_x":  {"group": "bw",       "iso": 250,  "blurb": "Cinema black and white with deep, moody shadows"},
    "neopan_acros_100":  {"group": "bw",       "iso": 100,  "blurb": "Ultra-fine grain with silvery midtones"},
    "hp5_plus":          {"group": "bw",       "iso": 400,  "blurb": "Forgiving black and white with soft, visible grain"},
    "fp4_plus_125":      {"group": "bw",       "iso": 125,  "blurb": "Fine grain, rich classic midtones"},
    "pan_f_plus_50":     {"group": "bw",       "iso": 50,   "blurb": "Extremely fine grain and high contrast"},
    "delta_100":         {"group": "bw",       "iso": 100,  "blurb": "Crisp, fine-grained modern black and white"},
    "delta_400":         {"group": "bw",       "iso": 400,  "blurb": "Clean modern tones at 400 speed"},
    "delta_3200":        {"group": "bw",       "iso": 3200, "blurb": "Coarse, atmospheric grain for low light"}
  }
}
```

- [ ] **Step 2: Write the failing test `desktop/tests/stock_catalog_test.cpp`**

```cpp
#include "StockCatalog.h"

#include <QDir>
#include <QtTest>

class StockCatalogTest : public QObject {
    Q_OBJECT
private slots:
    // Every engine stock profile must have a catalog entry (a new YAML without one
    // would appear in the Films browser with no group, ISO or description).
    void coversEveryStockProfile()
    {
        const QString root = QStringLiteral(DFEE_REPO_ROOT);
        const auto catalog = loadStockCatalog(root + "/desktop/resources/stocks/catalog.json");
        const QStringList yamls = QDir(root + "/profiles/stocks").entryList({"*.yaml"}, QDir::Files);
        QVERIFY(yamls.size() >= 33);
        for (const QString& f : yamls) {
            const QString id = f.chopped(5);
            QVERIFY2(catalog.contains(id), qPrintable("missing catalog entry: " + id));
            const StockInfo info = catalog.value(id);
            QVERIFY2(QStringList({"negative", "slide", "bw"}).contains(info.group), qPrintable(id));
            QVERIFY2(info.iso > 0, qPrintable(id));
            QVERIFY2(!info.blurb.isEmpty() && info.blurb.size() <= 60, qPrintable(id));
        }
    }

    void missingFileGivesEmptyCatalog()
    {
        QVERIFY(loadStockCatalog("Z:/no/such/catalog.json").isEmpty());
    }

    void groupLabels()
    {
        QCOMPARE(stockGroupLabel("negative"), QString("Color negative"));
        QCOMPARE(stockGroupLabel("slide"), QString("Slide"));
        QCOMPARE(stockGroupLabel("bw"), QString("B&W"));
    }
};

QTEST_GUILESS_MAIN(StockCatalogTest)
#include "stock_catalog_test.moc"
```

Append to `desktop/CMakeLists.txt`:

```cmake
qt_add_executable(stock_catalog_tests
    tests/stock_catalog_test.cpp
    src/StockCatalog.cpp
    src/StockCatalog.h
)
target_include_directories(stock_catalog_tests PRIVATE src)
target_link_libraries(stock_catalog_tests PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(stock_catalog_tests PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME stock_catalog_tests COMMAND stock_catalog_tests)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build desktop/out/build --config Release --target stock_catalog_tests`
Expected: compile error — `StockCatalog.h` not found.

- [ ] **Step 4: Create `desktop/src/StockCatalog.h`**

```cpp
#pragma once

#include <QHash>
#include <QString>

// Desktop-side presentation data for film stocks (the engine's profile loader
// rejects unknown YAML fields, so this lives in desktop/resources/stocks/catalog.json).
struct StockInfo {
    QString group;   // negative | slide | bw
    int iso = 0;
    QString blurb;   // one line describing the look
};

// Missing or invalid file -> empty catalog (the UI falls back to the engine type).
QHash<QString, StockInfo> loadStockCatalog(const QString& path);
QString stockGroupLabel(const QString& group);
```

- [ ] **Step 5: Create `desktop/src/StockCatalog.cpp`**

```cpp
#include "StockCatalog.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QHash<QString, StockInfo> loadStockCatalog(const QString& path)
{
    QHash<QString, StockInfo> catalog;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return catalog;
    const QJsonObject stocks = QJsonDocument::fromJson(f.readAll()).object().value("stocks").toObject();
    for (auto it = stocks.constBegin(); it != stocks.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        catalog.insert(it.key(), StockInfo{o.value("group").toString(), o.value("iso").toInt(),
                                           o.value("blurb").toString()});
    }
    return catalog;
}

QString stockGroupLabel(const QString& group)
{
    if (group == QLatin1String("negative")) return QStringLiteral("Color negative");
    if (group == QLatin1String("slide")) return QStringLiteral("Slide");
    if (group == QLatin1String("bw")) return QStringLiteral("B&W");
    return QString();
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build desktop/out/build --config Release --target stock_catalog_tests` then `desktop/out/build/Release/stock_catalog_tests.exe`
Expected: `Totals: 5 passed, 0 failed`.

- [ ] **Step 7: Bundle the catalog and merge it into the stock model**

In `desktop/CMakeLists.txt` add `resources/stocks/catalog.json` to the `qt_add_resources(DFEE "app_assets" ... FILES ...)` list (it maps to `:/stocks/catalog.json`), and add `src/StockCatalog.cpp` / `src/StockCatalog.h` to `qt_add_executable(DFEE ...)`.

In `desktop/src/EngineController.cpp` add `#include "StockCatalog.h"`. In `loadStocks()`, before the `for (const char* cat : ...)` loop, add:

```cpp
    const QHash<QString, StockInfo> catalog = loadStockCatalog(QStringLiteral(":/stocks/catalog.json"));
    const auto fallbackGroup = [](const std::string& t) -> QString {
        if (t == "color_reversal") return QStringLiteral("slide");
        if (t == "monochrome") return QStringLiteral("bw");
        return QStringLiteral("negative");
    };
```

and inside the loop, after `m["typeLabel"] = typeLabel(s.stock_type);`:

```cpp
            const StockInfo info = catalog.value(m["id"].toString());
            const QString group = info.group.isEmpty() ? fallbackGroup(s.stock_type) : info.group;
            m["group"] = group;
            m["groupLabel"] = stockGroupLabel(group);
            m["iso"] = info.iso;
            m["blurb"] = info.blurb;
```

- [ ] **Step 8: UI script check — add to `desktop/tests/ui/info.script` before `quit`**

```
expect:engine.stockModel.length=34
```

(33 stocks plus "None".) Run the info script as in Task 4 Step 8. Expected: `failures=0`.

- [ ] **Step 9: Commit**

```bash
git add desktop/resources/stocks/catalog.json desktop/src/StockCatalog.h desktop/src/StockCatalog.cpp desktop/tests/stock_catalog_test.cpp desktop/src/EngineController.cpp desktop/CMakeLists.txt desktop/tests/ui/info.script
git commit -m "feat(desktop): stock catalog with group, ISO and a one-line look per film"
```

---

### Task 6: Package and ship

**Files:**
- Modify: none expected (verify `desktop/packaging/deploy.ps1` output)

- [ ] **Step 1: Run every desktop test and UI script**

Run: `ctest -C Release --test-dir desktop/out/build` (with `C:\Qt\6.8.3\msvc2022_64\bin` on PATH), then each of `smoke.script`, `memory.script`, `info.script`, and `lightroom_bypass.script` (with `-AppArgs @('--lightroom-edit','${SAMPLE_TIFF}')`) through `run_ui.ps1`.
Expected: 3 ctest suites pass; every script prints `failures=0`.

- [ ] **Step 2: Deploy and confirm the SQLite driver ships**

Run: `powershell -ExecutionPolicy Bypass -File desktop/packaging/deploy.ps1`, then
`Test-Path desktop/out/dist/FilmLab/sqldrivers/qsqlite.dll` and `Test-Path desktop/out/dist/FilmLab/Qt6Sql.dll`.
Expected: both `True`. If `qsqlite.dll` is missing, add this line to deploy.ps1 right after the windeployqt call, then re-run the deploy:
`Copy-Item (Join-Path $Qt "plugins\sqldrivers\qsqlite.dll") (New-Item -ItemType Directory -Force (Join-Path $dist "sqldrivers")) -Force`

- [ ] **Step 3: Smoke-test the deployed build offscreen with the memory script**

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/memory.script -Exe desktop/out/dist/FilmLab/FilmLab.exe`
Expected: `failures=0` (proves the deployed folder carries the SQLite driver).

- [ ] **Step 4: Build the installer**

Run: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" desktop/packaging/FilmLab.iss`
Expected: `Successful compile ... FilmLab-Setup.exe`.

- [ ] **Step 5: Commit (only if deploy.ps1 changed)**

```bash
git add desktop/packaging/deploy.ps1
git commit -m "build(desktop): ship the SQLite driver for the edit catalog"
```
