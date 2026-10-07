# Export E1: destination, naming, remembering — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Exports go to a remembered folder (default `Pictures\Film Lab Exports`, favourites, recents, or next to the original), are named from a template with a live example, handle a taken name (add number / replace / skip), remember format/quality/dpi, and offer "Show in folder" afterwards.

**Architecture:** A new C++ `ExportPrefs` QObject (context property `exportPrefs`) owns every remembered export choice and persists it with `QSettings` group `export`. Pure `ExportNaming` functions expand the template, sanitise names and resolve collisions. `EngineController` builds the target path from both and passes it to the engine as `output_path`. The engine gains `write_report` (the desktop stops writing `_report.json` beside photos), always cleans its temp file, and reports the capture time so `{date}` works.

**Tech Stack:** C++20, Qt 6.8.3 (Core, Quick, QtTest), QML module `DFEE` (v2 files under `desktop/qml/v2/`), LibRaw, OpenCV imgcodecs.

**Spec:** `docs/superpowers/specs/2026-10-07-export-design.md` (sections Destination, File name, Format, Persistence; phase E1).

## Global Constraints

- Default folder: `QStandardPaths::PicturesLocation` + `/Film Lab Exports`; `DFEE_EXPORT_DIR` overrides it (tests).
- Settings: `QSettings` group `export`; the `DFEE_UI_SETTINGS` ini (IniFormat) when that env var is set, else the app's QSettings (org/app "Film Lab").
- Name template tokens in E1: `{name}` `{film}` `{date}` `{seq}` `{camera}`; default `{name}_{film}`; `{seq}` is 4 digits; `{date}` is `yyyy-MM-dd` (capture date, else file modified date).
- Collision rules: `number` (default; `-2`, `-3`, …), `replace`, `skip`.
- Formats `jpeg` (default), `png8`, `png16`, `tiff`; JPEG quality 1–100 (default 92); dpi 1–65535 (default 300).
- Recents: most recent first, at most 6, de-duplicated case-insensitively.
- Lightroom Edit-In is unchanged: Ctrl+S writes 16-bit TIFF back to the working file; it never uses `ExportPrefs` folder/naming.
- Copy uses US spelling in code identifiers (`favorites`), UK in visible copy only where the app already does; visible labels: "Save to", "File name", "If the name exists", "Favourites" group caption, "Show in folder".
- Never stage the user's WIP: `cpp_engine/bindings/python/dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`, `0005_A.aDil_The-Kodachrome-Project.jpg`. No `Co-Authored-By` lines in commits.
- Engine: build only `dfee_session_tests` / `dfee_tests` in `cpp_engine/out/build/windows-msvc-vcpkg` (never `dfee_native`). Throwing `expect()` checks, not `assert`.
- Desktop: always rebuild Release (`cmake --build desktop/out/build --config Release --target DFEE`). UI tests run offscreen via `desktop/tests/run_ui.ps1`; no real exports in UI tests.
- Source edits: Edit/Write tools or a Python script file — never PowerShell Get/Set-Content, never bash heredocs of Python.

## Review Focus

1. A template that expands to nothing or to a Windows-reserved/illegal name (`{camera}` on a TIFF, `con`, `a:b`) must still produce a valid, non-empty file name — pinned in Task 2 `sanitizeMakesWindowsSafeNames` / `emptyTemplatePartsLeaveNoStraySeparators`.
2. Clearing the File name field while typing must not snap back to the default template — pinned in Task 3 `blankTemplateIsKeptButResolvesToDefault`.
3. Picking from the folder dropdown inside the modal sheet must not break Esc-to-close — pinned in Task 5 `v2_export_sheet.script` (Esc after the picker).
4. "Skip" with a taken name must disable Export rather than silently do nothing — pinned in Task 5 `v2_export_sheet.script` (`exportConfirm.enabled=false`).
5. A favourite or recent folder that was deleted or is on an unplugged drive: recents drop missing folders on load (pinned in Task 3 `recentsDropMissingFoldersOnLoad`); exporting to a missing folder recreates it with `mkpath`, and when that fails the status reads "Export failed: can't create the folder …" instead of the engine's generic error (Task 4; no offscreen test can unplug a drive, so the reviewer checks this branch by reading it).

---

### Task 1: Engine — `write_report`, temp-file cleanup, capture time

**Files:**
- Modify: `cpp_engine/include/dfee/bridge_types.hpp` (`NativeRawMetadata` ~52-75, `NativeExportRequest` ~355-366)
- Modify: `cpp_engine/src/raw_decode.cpp` (`fill_metadata_from_raw_processor` ~110-125)
- Modify: `cpp_engine/src/session.cpp` (`export_image` write block ~3589-3685, report ~3686)
- Test: `cpp_engine/tests/test_session.cpp`

**Interfaces:**
- Produces: `NativeExportRequest::write_report` (bool, default `true`); `NativeRawMetadata::capture_timestamp` (`std::int64_t`, seconds since epoch as LibRaw reads EXIF DateTimeOriginal in local time; `0` = unknown).

- [ ] **Step 1: Write the failing test** — add to `test_session.cpp` before `}  // namespace`:

```cpp
// The desktop app picks the file and keeps no JSON report beside the photo; the
// Python/server callers keep today's report. No temp file survives an export.
void test_export_destination_and_report() {
    const auto file = write_scene("export", 0, 240, 160);
    const auto out_dir = std::filesystem::temp_directory_path() / "dfee_session_export_test";
    std::filesystem::remove_all(out_dir);
    std::filesystem::create_directories(out_dir);
    const auto report = file.parent_path() / (file.stem().string() + "_portra_400_report.json");
    std::filesystem::remove(report);
    dfee::EngineSession session(kRepoRoot);

    dfee::NativeExportRequest r;
    static_cast<dfee::NativePreviewRenderRequest&>(r) = base_request(file, "portra_400");
    r.export_format = "jpeg";
    r.output_path = out_dir / "chosen name.jpg";
    r.write_report = false;
    auto res = session.export_image(r);
    expect(res.ok, "export to a chosen path: " + res.error.detail);
    expect(std::filesystem::exists(out_dir / "chosen name.jpg"), "file lands at output_path");
    expect(!std::filesystem::exists(report), "no report when write_report=false");
    const cv::Mat img = cv::imread((out_dir / "chosen name.jpg").string(), cv::IMREAD_UNCHANGED);
    expect(img.cols == 240 && img.rows == 160, "full-size export");

    r.write_report = true;
    res = session.export_image(r);
    expect(res.ok, "second export: " + res.error.detail);
    expect(std::filesystem::exists(report), "report still written by default");
    for (const auto& e : std::filesystem::directory_iterator(out_dir)) {
        expect(e.path().filename().string().find(".dfee-writing-") == std::string::npos,
               "no temp file left: " + e.path().string());
    }
    std::filesystem::remove(report);
    std::filesystem::remove_all(out_dir);
    std::filesystem::remove(file);
}
```

and call it in `main()` after `test_proxy_speed_and_preview_untouched();`:

```cpp
        test_export_destination_and_report();
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests`
Expected: compile error — `'write_report': is not a member of 'dfee::NativeExportRequest'`.

- [ ] **Step 3: Implement**

In `bridge_types.hpp`, inside `NativeRawMetadata` after `focal_length`:

```cpp
    // Capture time (EXIF DateTimeOriginal as LibRaw reads it, local time, seconds since
    // the epoch); 0 when the file has none (rendered TIFFs).
    std::int64_t capture_timestamp = 0;
```

Inside `NativeExportRequest` after `output_path`:

```cpp
    // The JSON feature report beside the source (diagnostics for the Python/server
    // callers). The desktop app keeps its own records and turns it off.
    bool write_report = true;
```

In `raw_decode.cpp` `fill_metadata_from_raw_processor`, after the `focal_length` line:

```cpp
    metadata.capture_timestamp = other.timestamp > 0 ? static_cast<std::int64_t>(other.timestamp) : 0;
```

In `session.cpp` `export_image`, directly after the existing
`std::filesystem::remove(temporary_output, cleanup_error);` line that follows the
`temporary_output` declaration, add:

```cpp
            // Every exit below (encode or metadata failure, or success after the atomic
            // replace has moved the file away) leaves no half-written temp file behind.
            struct TemporaryOutputCleanup {
                const std::filesystem::path& path;
                ~TemporaryOutputCleanup() {
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                }
            } temporary_output_cleanup{temporary_output};
```

Change the report condition from
`if (render_plan.has_value() && solver_input.has_value() && stock_profile.has_value()) {`
to
`if (request.write_report && render_plan.has_value() && solver_input.has_value() && stock_profile.has_value()) {`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests dfee_tests` then `cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe` and `cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_tests.exe`
Expected: `dfee_session_tests: all passed` (goldens 24/24 unchanged) and `dfee_tests` exit 0.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/include/dfee/bridge_types.hpp cpp_engine/src/raw_decode.cpp cpp_engine/src/session.cpp cpp_engine/tests/test_session.cpp
git commit -m "feat(engine): export can skip the JSON report; temp file always cleaned; RAW capture time"
```

---

### Task 2: `ExportNaming` — templates, safe names, collisions

**Files:**
- Create: `desktop/src/ExportNaming.h`, `desktop/src/ExportNaming.cpp`
- Create: `desktop/tests/export_test.cpp`
- Modify: `desktop/CMakeLists.txt` (new `export_tests` target after `stock_catalog_tests`; add `src/ExportNaming.cpp src/ExportNaming.h` to the `DFEE` executable's sources next to `src/EditStore.cpp`)

**Interfaces:**
- Produces (namespace `ExportNaming`):
  - `struct Tokens { QString name; QString film; QString date; QString camera; int sequence = 1; };`
  - `enum class Collision { AddNumber, Replace, Skip };`
  - `struct Target { QString path; QString fileName; bool exists = false; bool skip = false; };`
  - `QString expand(const QString& pattern, const Tokens& tokens);` — returns a sanitised stem (no extension)
  - `QString sanitize(const QString& name);`
  - `QString extensionFor(const QString& format);` — `.jpg` / `.png` / `.tif`
  - `Collision collisionFromString(const QString& rule);` — `"replace"`, `"skip"`, anything else AddNumber
  - `Target resolveTarget(const QString& folder, const QString& stem, const QString& extension, Collision rule);`

- [ ] **Step 1: Write the failing test** — `desktop/tests/export_test.cpp`:

```cpp
#include "ExportNaming.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void touch(const QString& path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
}
}

class ExportTest : public QObject {
    Q_OBJECT
private slots:
    void expandsTokens()
    {
        ExportNaming::Tokens t;
        t.name = "DSC0421";
        t.film = "Kodak Portra 400";
        t.date = "2026-05-03";
        t.camera = "Sony ILCE-7CR";
        t.sequence = 7;
        QCOMPARE(ExportNaming::expand("{name}_{film}", t), QString("DSC0421_Kodak Portra 400"));
        QCOMPARE(ExportNaming::expand("{date} {name}-{seq}", t), QString("2026-05-03 DSC0421-0007"));
        QCOMPARE(ExportNaming::expand("{camera}", t), QString("Sony ILCE-7CR"));
        QCOMPARE(ExportNaming::expand("Roll 3 {name}", t), QString("Roll 3 DSC0421"));
    }

    void emptyTemplatePartsLeaveNoStraySeparators()
    {
        ExportNaming::Tokens t;
        t.name = "DSC0421";
        QCOMPARE(ExportNaming::expand("{name}_{film}", t), QString("DSC0421"));
        QCOMPARE(ExportNaming::expand("{name}_{camera}_{film}", t), QString("DSC0421"));
        QCOMPARE(ExportNaming::expand("{film} - {name}", t), QString("DSC0421"));
        QCOMPARE(ExportNaming::expand("{camera}", t), QString("export"));
        QCOMPARE(ExportNaming::expand("", t), QString("export"));
    }

    void sanitizeMakesWindowsSafeNames()
    {
        QCOMPARE(ExportNaming::sanitize("a/b\\c:d*e?f\"g<h>i|j"), QString("a-b-c-d-e-f-g-h-i-j"));
        QCOMPARE(ExportNaming::sanitize("name. . "), QString("name"));
        QCOMPARE(ExportNaming::sanitize("con"), QString("con_"));
        QCOMPARE(ExportNaming::sanitize("LPT1"), QString("LPT1_"));
        QCOMPARE(ExportNaming::sanitize(QString(250, 'x')).size(), 180);
        QCOMPARE(ExportNaming::sanitize("   "), QString("export"));
    }

    void extensions()
    {
        QCOMPARE(ExportNaming::extensionFor("jpeg"), QString(".jpg"));
        QCOMPARE(ExportNaming::extensionFor("png8"), QString(".png"));
        QCOMPARE(ExportNaming::extensionFor("png16"), QString(".png"));
        QCOMPARE(ExportNaming::extensionFor("tiff"), QString(".tif"));
    }

    void collisionRules()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        using ExportNaming::Collision;
        auto free = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::AddNumber);
        QCOMPARE(free.fileName, QString("a.jpg"));
        QVERIFY(!free.exists);
        QVERIFY(!free.skip);

        touch(dir.filePath("a.jpg"));
        auto numbered = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::AddNumber);
        QCOMPARE(numbered.fileName, QString("a-2.jpg"));
        QCOMPARE(numbered.path, dir.filePath("a-2.jpg"));
        QVERIFY(numbered.exists);
        touch(dir.filePath("a-2.jpg"));
        QCOMPARE(ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::AddNumber).fileName,
                 QString("a-3.jpg"));

        auto replace = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::Replace);
        QCOMPARE(replace.fileName, QString("a.jpg"));
        QVERIFY(replace.exists);
        QVERIFY(!replace.skip);

        auto skip = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::Skip);
        QVERIFY(skip.exists);
        QVERIFY(skip.skip);

        QCOMPARE(ExportNaming::collisionFromString("replace"), Collision::Replace);
        QCOMPARE(ExportNaming::collisionFromString("skip"), Collision::Skip);
        QCOMPARE(ExportNaming::collisionFromString("number"), Collision::AddNumber);
        QCOMPARE(ExportNaming::collisionFromString("bogus"), Collision::AddNumber);
    }
};

QTEST_GUILESS_MAIN(ExportTest)
#include "export_test.moc"
```

Add to `desktop/CMakeLists.txt` after the `stock_catalog_tests` block:

```cmake
qt_add_executable(export_tests
    tests/export_test.cpp
    src/ExportNaming.cpp
    src/ExportNaming.h
)
target_include_directories(export_tests PRIVATE src)
target_link_libraries(export_tests PRIVATE Qt6::Core Qt6::Test)
add_test(NAME export_tests COMMAND export_tests)
```

and add `src/ExportNaming.cpp` / `src/ExportNaming.h` to the `DFEE` executable's source list (beside `src/EditStore.cpp`).

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build desktop/out/build --config Release --target export_tests`
Expected: FAIL — `Cannot open include file: 'ExportNaming.h'` (or CMake "Cannot find source file").

- [ ] **Step 3: Implement** — `desktop/src/ExportNaming.h`:

```cpp
#pragma once

#include <QString>

// Export file names: the template and its tokens, Windows-safe names, and what
// happens when the name is already taken. Pure functions (export_tests).
namespace ExportNaming {

struct Tokens {
    QString name;      // source file stem
    QString film;      // stock display name; empty for no film
    QString date;      // yyyy-MM-dd
    QString camera;    // "Sony ILCE-7CR"; may be empty
    int sequence = 1;  // {seq}, written with 4 digits
};

enum class Collision { AddNumber, Replace, Skip };

struct Target {
    QString path;          // folder + fileName
    QString fileName;
    bool exists = false;   // a file already has the template's name
    bool skip = false;     // Skip rule (or no free number): nothing is written
};

QString expand(const QString& pattern, const Tokens& tokens);
QString sanitize(const QString& name);
QString extensionFor(const QString& format);
Collision collisionFromString(const QString& rule);
Target resolveTarget(const QString& folder, const QString& stem, const QString& extension, Collision rule);

}  // namespace ExportNaming
```

`desktop/src/ExportNaming.cpp`:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build desktop/out/build --config Release --target export_tests` then `desktop/out/build/Release/export_tests.exe`
Expected: `Totals: 7 passed, 0 failed` (5 tests + initTestCase/cleanupTestCase).

- [ ] **Step 5: Commit**

```bash
git add desktop/src/ExportNaming.h desktop/src/ExportNaming.cpp desktop/tests/export_test.cpp desktop/CMakeLists.txt
git commit -m "feat(desktop): export file-name templates, Windows-safe names, collision rules"
```

---

### Task 3: `ExportPrefs` — remembered folder, favourites, recents, format

**Files:**
- Create: `desktop/src/ExportPrefs.h`, `desktop/src/ExportPrefs.cpp`
- Modify: `desktop/tests/export_test.cpp` (new slots), `desktop/CMakeLists.txt` (add `src/ExportPrefs.cpp src/ExportPrefs.h` to both `export_tests` and `DFEE`)

**Interfaces:**
- Consumes: nothing from Task 2.
- Produces: class `ExportPrefs : QObject`, ctor `ExportPrefs(const QString& iniPath = {}, QObject* parent = nullptr)`; signal `changed()`; properties `folder` (QString, chosen; `""` = default), `folderPath` (effective chosen-or-default folder, `/` separators), `nextToOriginal` (bool), `defaultFolder` (CONSTANT), `nameTemplate` (RW, stored verbatim), `collision` (RW `number|replace|skip`), `format` (RW `jpeg|png8|png16|tiff`), `jpegQuality` (RW), `dpi` (RW), `favorites` (QStringList), `recents` (QStringList), `sequence` (int); invokables `useFolder(QString)`, `useFolderUrl(QUrl)`, `useDefaultFolder()`, `useNextToOriginal()`, `toggleFavorite(QString)`, `isFavorite(QString) const`; C++ `QString effectiveNameTemplate() const`, `QString targetFolder(const QString& sourcePath) const`, `void noteExported(const QString& outputPath)`; `static QString defaultFolderPath()`; `static constexpr int kMaxRecents = 6`.

- [ ] **Step 1: Write the failing tests** — in `export_test.cpp` add `#include "ExportPrefs.h"` and these slots (plus an `initTestCase` that sets the default-folder override):

```cpp
    void initTestCase()
    {
        QVERIFY(exportsRoot.isValid());
        qputenv("DFEE_EXPORT_DIR", exportsRoot.filePath("default").toUtf8());
    }

    void prefsDefaults()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        QCOMPARE(p.defaultFolder(), QDir::fromNativeSeparators(exportsRoot.filePath("default")));
        QCOMPARE(p.folderPath(), p.defaultFolder());
        QVERIFY(!p.nextToOriginal());
        QCOMPARE(p.nameTemplate(), QString("{name}_{film}"));
        QCOMPARE(p.collision(), QString("number"));
        QCOMPARE(p.format(), QString("jpeg"));
        QCOMPARE(p.jpegQuality(), 92);
        QCOMPARE(p.dpi(), 300);
        QCOMPARE(p.sequence(), 1);
        QVERIFY(p.favorites().isEmpty());
    }

    void prefsSurviveRestart()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath("ui.ini");
        const QString shoot = QDir::fromNativeSeparators(dir.filePath("shoot"));
        QDir().mkpath(shoot);
        {
            ExportPrefs p(ini);
            p.setNameTemplate("{date}_{name}");
            p.setCollision("skip");
            p.setFormat("tiff");
            p.setJpegQuality(80);
            p.setDpi(600);
            p.useFolder(shoot);
            p.toggleFavorite(shoot);
            p.noteExported(shoot + "/a.tif");
        }
        ExportPrefs q(ini);
        QCOMPARE(q.nameTemplate(), QString("{date}_{name}"));
        QCOMPARE(q.collision(), QString("skip"));
        QCOMPARE(q.format(), QString("tiff"));
        QCOMPARE(q.jpegQuality(), 80);
        QCOMPARE(q.dpi(), 600);
        QCOMPARE(q.folderPath(), shoot);
        QVERIFY(q.isFavorite(shoot));
        QCOMPARE(q.recents(), QStringList{shoot});
        QCOMPARE(q.sequence(), 2);
    }

    void invalidValuesFallBack()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.setFormat("gif");
        QCOMPARE(p.format(), QString("jpeg"));
        p.setCollision("maybe");
        QCOMPARE(p.collision(), QString("number"));
        p.setJpegQuality(500);
        QCOMPARE(p.jpegQuality(), 100);
        p.setDpi(0);
        QCOMPARE(p.dpi(), 1);
    }

    void blankTemplateIsKeptButResolvesToDefault()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.setNameTemplate("");
        QCOMPARE(p.nameTemplate(), QString(""));          // the field is not snapped back while typing
        QCOMPARE(p.effectiveNameTemplate(), QString("{name}_{film}"));
    }

    void nextToOriginalAndFolders()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.useNextToOriginal();
        QVERIFY(p.nextToOriginal());
        QCOMPARE(p.targetFolder("E:/new_raws/DSC0421.ARW"), QString("E:/new_raws"));
        const QString elsewhere = QDir::fromNativeSeparators(dir.filePath("x"));
        p.useFolder(QDir::toNativeSeparators(elsewhere));
        QVERIFY(!p.nextToOriginal());
        QCOMPARE(p.folderPath(), elsewhere);
        QCOMPARE(p.targetFolder("E:/new_raws/DSC0421.ARW"), elsewhere);
        p.useDefaultFolder();
        QCOMPARE(p.folderPath(), p.defaultFolder());
    }

    void favoritesToggle()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.toggleFavorite("D:/Prints");
        QVERIFY(p.isFavorite("d:\\prints\\"));               // case and separators don't matter
        p.toggleFavorite("d:/prints");
        QVERIFY(p.favorites().isEmpty());
    }

    void recentsAreMostRecentFirstCappedAndDeduplicated()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        QStringList folders;
        for (int i = 0; i < 8; ++i) {
            folders << QDir::fromNativeSeparators(dir.filePath(QString("f%1").arg(i)));
            QDir().mkpath(folders.last());
            p.noteExported(folders.last() + "/a.jpg");
        }
        QCOMPARE(p.recents().size(), ExportPrefs::kMaxRecents);
        QCOMPARE(p.recents().first(), folders[7]);
        p.noteExported(folders[5].toUpper() + "/b.jpg");
        QCOMPARE(p.recents().size(), ExportPrefs::kMaxRecents);
        QVERIFY(p.recents().first().compare(folders[5], Qt::CaseInsensitive) == 0);
        QCOMPARE(p.sequence(), 10);
    }

    void recentsDropMissingFoldersOnLoad()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath("ui.ini");
        const QString gone = QDir::fromNativeSeparators(dir.filePath("gone"));
        QDir().mkpath(gone);
        {
            ExportPrefs p(ini);
            p.noteExported(gone + "/a.jpg");
        }
        QDir(gone).removeRecursively();
        ExportPrefs q(ini);
        QVERIFY(q.recents().isEmpty());
    }
```

with a member in the test class: `QTemporaryDir exportsRoot;` (declare `private:` above `private slots:`; add `#include <QDir>`).

Add `src/ExportPrefs.cpp` and `src/ExportPrefs.h` to `export_tests` sources and to the `DFEE` executable sources.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build desktop/out/build --config Release --target export_tests`
Expected: FAIL — `Cannot open include file: 'ExportPrefs.h'`.

- [ ] **Step 3: Implement** — `desktop/src/ExportPrefs.h`:

```cpp
#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

#include <memory>

class QSettings;

// Export destination, naming and format choices, remembered between launches in
// QSettings group "export" (the DFEE_UI_SETTINGS ini in tests). Paths are kept with
// '/' separators and compared case-insensitively (Windows).
class ExportPrefs : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY changed)
    Q_PROPERTY(bool nextToOriginal READ nextToOriginal NOTIFY changed)
    Q_PROPERTY(QString defaultFolder READ defaultFolder CONSTANT)
    Q_PROPERTY(QString nameTemplate READ nameTemplate WRITE setNameTemplate NOTIFY changed)
    Q_PROPERTY(QString collision READ collision WRITE setCollision NOTIFY changed)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY changed)
    Q_PROPERTY(int jpegQuality READ jpegQuality WRITE setJpegQuality NOTIFY changed)
    Q_PROPERTY(int dpi READ dpi WRITE setDpi NOTIFY changed)
    Q_PROPERTY(QStringList favorites READ favorites NOTIFY changed)
    Q_PROPERTY(QStringList recents READ recents NOTIFY changed)
    Q_PROPERTY(int sequence READ sequence NOTIFY changed)
public:
    static constexpr int kMaxRecents = 6;
    // iniPath empty: the app's QSettings; otherwise that ini file.
    explicit ExportPrefs(const QString& iniPath = {}, QObject* parent = nullptr);
    ~ExportPrefs() override;

    static QString defaultFolderPath();

    QString folder() const { return folder_; }
    QString folderPath() const { return folder_.isEmpty() ? defaultFolder_ : folder_; }
    bool nextToOriginal() const { return nextToOriginal_; }
    QString defaultFolder() const { return defaultFolder_; }
    QString nameTemplate() const { return nameTemplate_; }
    void setNameTemplate(const QString& pattern);
    QString effectiveNameTemplate() const;
    QString collision() const { return collision_; }
    void setCollision(const QString& rule);
    QString format() const { return format_; }
    void setFormat(const QString& format);
    int jpegQuality() const { return jpegQuality_; }
    void setJpegQuality(int quality);
    int dpi() const { return dpi_; }
    void setDpi(int dpi);
    QStringList favorites() const { return favorites_; }
    QStringList recents() const { return recents_; }
    int sequence() const { return sequence_; }

    Q_INVOKABLE void useFolder(const QString& path);
    Q_INVOKABLE void useFolderUrl(const QUrl& url);
    Q_INVOKABLE void useDefaultFolder();
    Q_INVOKABLE void useNextToOriginal();
    Q_INVOKABLE void toggleFavorite(const QString& path);
    Q_INVOKABLE bool isFavorite(const QString& path) const;

    // Where an export of sourcePath goes.
    QString targetFolder(const QString& sourcePath) const;
    // After a successful export: its folder becomes the most recent, {seq} advances.
    void noteExported(const QString& outputPath);

signals:
    void changed();

private:
    void save();
    std::unique_ptr<QSettings> settings_;
    QString defaultFolder_;
    QString folder_;
    bool nextToOriginal_ = false;
    QString nameTemplate_;
    QString collision_;
    QString format_;
    int jpegQuality_ = 92;
    int dpi_ = 300;
    QStringList favorites_;
    QStringList recents_;
    int sequence_ = 1;
};
```

`desktop/src/ExportPrefs.cpp`:

```cpp
#include "ExportPrefs.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {
const QString kDefaultTemplate = QStringLiteral("{name}_{film}");
const QStringList kFormats{QStringLiteral("jpeg"), QStringLiteral("png8"), QStringLiteral("png16"), QStringLiteral("tiff")};
const QStringList kCollisions{QStringLiteral("number"), QStringLiteral("replace"), QStringLiteral("skip")};

QString normalized(const QString& path)
{
    if (path.trimmed().isEmpty()) return {};
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (p.size() > 3 && p.endsWith(QLatin1Char('/'))) p.chop(1);
    return p;
}

bool samePath(const QString& a, const QString& b)
{
    return normalized(a).compare(normalized(b), Qt::CaseInsensitive) == 0;
}

int indexOfPath(const QStringList& list, const QString& path)
{
    for (int i = 0; i < list.size(); ++i)
        if (samePath(list.at(i), path)) return i;
    return -1;
}
}  // namespace

ExportPrefs::ExportPrefs(const QString& iniPath, QObject* parent)
    : QObject(parent),
      settings_(iniPath.isEmpty() ? std::make_unique<QSettings>()
                                  : std::make_unique<QSettings>(iniPath, QSettings::IniFormat)),
      defaultFolder_(defaultFolderPath())
{
    QSettings& s = *settings_;
    s.beginGroup(QStringLiteral("export"));
    folder_ = normalized(s.value(QStringLiteral("folder")).toString());
    nextToOriginal_ = s.value(QStringLiteral("nextToOriginal"), false).toBool();
    nameTemplate_ = s.value(QStringLiteral("nameTemplate"), kDefaultTemplate).toString();
    const QString collision = s.value(QStringLiteral("collision")).toString();
    collision_ = kCollisions.contains(collision) ? collision : kCollisions.first();
    const QString format = s.value(QStringLiteral("format")).toString();
    format_ = kFormats.contains(format) ? format : kFormats.first();
    jpegQuality_ = std::clamp(s.value(QStringLiteral("jpegQuality"), 92).toInt(), 1, 100);
    dpi_ = std::clamp(s.value(QStringLiteral("dpi"), 300).toInt(), 1, 65535);
    for (const QString& f : s.value(QStringLiteral("favorites")).toStringList()) favorites_ << normalized(f);
    for (const QString& r : s.value(QStringLiteral("recents")).toStringList())
        if (QFileInfo(r).isDir()) recents_ << normalized(r);     // drop folders that are gone
    sequence_ = std::max(1, s.value(QStringLiteral("sequence"), 1).toInt());
    s.endGroup();
}

ExportPrefs::~ExportPrefs() = default;

QString ExportPrefs::defaultFolderPath()
{
    const QString overridePath = qEnvironmentVariable("DFEE_EXPORT_DIR");
    if (!overridePath.isEmpty()) return normalized(overridePath);
    return normalized(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
                      QStringLiteral("/Film Lab Exports"));
}

void ExportPrefs::save()
{
    QSettings& s = *settings_;
    s.beginGroup(QStringLiteral("export"));
    s.setValue(QStringLiteral("folder"), folder_);
    s.setValue(QStringLiteral("nextToOriginal"), nextToOriginal_);
    s.setValue(QStringLiteral("nameTemplate"), nameTemplate_);
    s.setValue(QStringLiteral("collision"), collision_);
    s.setValue(QStringLiteral("format"), format_);
    s.setValue(QStringLiteral("jpegQuality"), jpegQuality_);
    s.setValue(QStringLiteral("dpi"), dpi_);
    s.setValue(QStringLiteral("favorites"), favorites_);
    s.setValue(QStringLiteral("recents"), recents_);
    s.setValue(QStringLiteral("sequence"), sequence_);
    s.endGroup();
    s.sync();
    emit changed();
}

void ExportPrefs::setNameTemplate(const QString& pattern)
{
    if (nameTemplate_ == pattern) return;
    nameTemplate_ = pattern;   // kept verbatim: clearing the field must not snap it back
    save();
}

QString ExportPrefs::effectiveNameTemplate() const
{
    return nameTemplate_.trimmed().isEmpty() ? kDefaultTemplate : nameTemplate_;
}

void ExportPrefs::setCollision(const QString& rule)
{
    const QString value = kCollisions.contains(rule) ? rule : kCollisions.first();
    if (collision_ == value) return;
    collision_ = value;
    save();
}

void ExportPrefs::setFormat(const QString& format)
{
    const QString value = kFormats.contains(format) ? format : kFormats.first();
    if (format_ == value) return;
    format_ = value;
    save();
}

void ExportPrefs::setJpegQuality(int quality)
{
    const int value = std::clamp(quality, 1, 100);
    if (jpegQuality_ == value) return;
    jpegQuality_ = value;
    save();
}

void ExportPrefs::setDpi(int dpi)
{
    const int value = std::clamp(dpi, 1, 65535);
    if (dpi_ == value) return;
    dpi_ = value;
    save();
}

void ExportPrefs::useFolder(const QString& path)
{
    const QString p = normalized(path);
    if (p.isEmpty()) return;
    folder_ = samePath(p, defaultFolder_) ? QString() : p;
    nextToOriginal_ = false;
    save();
}

void ExportPrefs::useFolderUrl(const QUrl& url)
{
    useFolder(url.toLocalFile());
}

void ExportPrefs::useDefaultFolder()
{
    folder_.clear();
    nextToOriginal_ = false;
    save();
}

void ExportPrefs::useNextToOriginal()
{
    nextToOriginal_ = true;
    save();
}

void ExportPrefs::toggleFavorite(const QString& path)
{
    const QString p = normalized(path);
    if (p.isEmpty()) return;
    const int at = indexOfPath(favorites_, p);
    if (at >= 0) favorites_.removeAt(at);
    else favorites_ << p;
    save();
}

bool ExportPrefs::isFavorite(const QString& path) const
{
    return indexOfPath(favorites_, path) >= 0;
}

QString ExportPrefs::targetFolder(const QString& sourcePath) const
{
    return nextToOriginal_ ? normalized(QFileInfo(sourcePath).absolutePath()) : folderPath();
}

void ExportPrefs::noteExported(const QString& outputPath)
{
    const QString dir = normalized(QFileInfo(outputPath).absolutePath());
    const int at = indexOfPath(recents_, dir);
    if (at >= 0) recents_.removeAt(at);
    recents_.prepend(dir);
    while (recents_.size() > kMaxRecents) recents_.removeLast();
    ++sequence_;
    save();
}
```

Note for `nextToOriginalAndFolders`: `QFileInfo("E:/new_raws/DSC0421.ARW").absolutePath()` returns `E:/new_raws` without the file existing — correct.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build desktop/out/build --config Release --target export_tests` then `desktop/out/build/Release/export_tests.exe`
Expected: all slots PASS (`Totals: 15 passed, 0 failed`).

- [ ] **Step 5: Commit**

```bash
git add desktop/src/ExportPrefs.h desktop/src/ExportPrefs.cpp desktop/tests/export_test.cpp desktop/CMakeLists.txt
git commit -m "feat(desktop): ExportPrefs remembers folder, favourites, recents, name rule and format"
```

---

### Task 4: Wire exports through `ExportPrefs` (+ capture date in `imageInfo`)

**Files:**
- Modify: `desktop/src/ImageInfo.cpp`, `desktop/tests/image_info_test.cpp`
- Modify: `desktop/src/EngineController.h` / `.cpp` (export props → prefs; `exportTarget`, `lastExportPath`, `showLastExport`; `buildExportRequest`, `exportImage`, `onExportDone`)
- Modify: `desktop/src/main.cpp` (create `ExportPrefs`, context property `exportPrefs`, selftest hook)
- Modify: `desktop/tests/run_ui.ps1` (`DFEE_EXPORT_DIR` + a taken-name fixture)
- Modify: `desktop/qml/v2/dialogs/FlExportSheet.qml` (bindings only: `engine.exportFormat/jpegQuality/exportDpi` → `exportPrefs.format/jpegQuality/dpi`), `desktop/tests/ui/v2_sheets.script` (same rename)
- Test: `desktop/tests/ui/v2_export_target.script` (new)

**Interfaces:**
- Consumes: `ExportNaming::{Tokens, expand, extensionFor, collisionFromString, resolveTarget}` (Task 2); `ExportPrefs` (Task 3); `NativeRawMetadata::capture_timestamp`, `NativeExportRequest::write_report` (Task 1).
- Produces: `imageInfo.date` (`yyyy-MM-dd`, RAW only); `EngineController::setExportPrefs(ExportPrefs*)`; `Q_INVOKABLE QVariantMap exportTarget() const` → `{path, fileName, folder, exists, skip, replaces}`; `Q_PROPERTY(QString lastExportPath ... NOTIFY lastExportPathChanged)`; `Q_INVOKABLE void showLastExport() const`. Removed: `engine.exportFormat`, `engine.jpegQuality`, `engine.exportDpi`.

- [ ] **Step 1: Write the failing tests**

`image_info_test.cpp` — new slot:

```cpp
    void captureDateIsLocalDay()
    {
        dfee::NativeRawMetadata md;
        md.input_kind = "developed_raw";
        md.capture_timestamp = QDateTime(QDate(2026, 5, 3), QTime(12, 0)).toSecsSinceEpoch();
        QCOMPARE(imageInfoFromMetadata(md).value("date").toString(), QString("2026-05-03"));
        md.capture_timestamp = 0;
        QVERIFY(!imageInfoFromMetadata(md).contains("date"));
    }
```

`desktop/tests/ui/v2_export_target.script` (new; `run_ui.ps1` creates `${TEMP}/exports/taken.jpg`):

```
# The export target follows ExportPrefs: default folder (DFEE_EXPORT_DIR), the name
# template with the film, and the taken-name rules.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:exportPrefs.folderPath=${TEMP}/exports
expect:exportPrefs.format=jpeg
key:83+ctrl
wait:300
expect:exportSheet.visible=true
expect:exportNameExample.text=3071874357.jpg
stock:portra_400
wait:300
expect:exportNameExample.text=3071874357_Kodak Portra 400.jpg
set:exportPrefs.nameTemplate={seq} {name}
wait:200
expect:exportNameExample.text=0001 3071874357.jpg
set:exportPrefs.nameTemplate=taken
wait:200
expect:exportNameExample.text=taken-2.jpg
set:exportPrefs.collision=replace
wait:200
expect:exportNameExample.text=taken.jpg
expect:exportTargetNote.text=Will replace the existing file.
set:exportPrefs.collision=skip
wait:200
expect:exportConfirm.enabled=false
set:exportPrefs.collision=number
set:exportPrefs.nameTemplate={name}_{film}
key:16777216
wait:300
expect:exportSheet.visible=false
quit
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build desktop/out/build --config Release --target image_info_tests` → Expected: FAIL `captureDateIsLocalDay` (no `date` key).
Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_export_target.script` → Expected: `UISCRIPT FAIL expect:exportPrefs.folderPath=...` (no `exportPrefs` yet).

- [ ] **Step 3: Implement**

`ImageInfo.cpp`: add `#include <QDateTime>`; before `if (kind == QLatin1String("rendered")) return m;` nothing changes; after the `focal` line add:

```cpp
    if (md.capture_timestamp > 0)
        m["date"] = QDateTime::fromSecsSinceEpoch(md.capture_timestamp).toString(QStringLiteral("yyyy-MM-dd"));
```

`EngineController.h`:
- Remove the `exportFormat`, `jpegQuality`, `exportDpi` Q_PROPERTYs, their getters/setters and members `exportFormat_`, `jpegQuality_`, `exportDpi_`, and the `exportSettingsChanged` signal.
- Add `#include "ExportNaming.h"` and forward-declare `class ExportPrefs;`.
- Add:

```cpp
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY lastExportPathChanged)
    ...
    void setExportPrefs(ExportPrefs* prefs) { exportPrefs_ = prefs; }
    QString lastExportPath() const { return lastExportPath_; }
    // Where the next export of the open photo goes:
    // {path, fileName, folder, exists, skip, replaces}. Empty map without a photo.
    Q_INVOKABLE QVariantMap exportTarget() const;
    // Opens Explorer with the last exported file selected.
    Q_INVOKABLE void showLastExport() const;
    ...
signals:
    void lastExportPathChanged();
    ...
private:
    ExportNaming::Tokens exportTokens() const;
    ExportPrefs* exportPrefs_ = nullptr;
    QString lastExportPath_;
    QString pendingExportPath_;
```

`EngineController.cpp` (add `#include "ExportPrefs.h"`, `<QDir>`, `<QFileInfo>`, `<QProcess>`, `<QDateTime>`):
- Delete `setExportFormat`, `setJpegQuality`, `setExportDpi`.
- Add:

```cpp
ExportNaming::Tokens EngineController::exportTokens() const
{
    ExportNaming::Tokens t;
    const QFileInfo source(currentFile_);
    t.name = source.completeBaseName();
    if (stockId_ != QLatin1String("none")) {
        const int i = stockIds_.indexOf(stockId_);
        t.film = i >= 0 ? stockNames_.at(i) : stockId_;
    }
    t.date = imageInfo_.value(QStringLiteral("date")).toString();
    if (t.date.isEmpty()) t.date = source.lastModified().toString(QStringLiteral("yyyy-MM-dd"));
    t.camera = imageInfo_.value(QStringLiteral("camera")).toString();
    t.sequence = exportPrefs_ ? exportPrefs_->sequence() : 1;
    return t;
}

QVariantMap EngineController::exportTarget() const
{
    if (currentFile_.isEmpty() || !exportPrefs_) return {};
    const QString folder = exportPrefs_->targetFolder(currentFile_);
    const QString stem = ExportNaming::expand(exportPrefs_->effectiveNameTemplate(), exportTokens());
    const auto rule = ExportNaming::collisionFromString(exportPrefs_->collision());
    const auto target = ExportNaming::resolveTarget(
        folder, stem, ExportNaming::extensionFor(exportPrefs_->format()), rule);
    return {
        {QStringLiteral("path"), target.path},
        {QStringLiteral("fileName"), target.fileName},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("exists"), target.exists},
        {QStringLiteral("skip"), target.skip},
        {QStringLiteral("replaces"), target.exists && rule == ExportNaming::Collision::Replace},
    };
}

void EngineController::showLastExport() const
{
    if (lastExportPath_.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(lastExportPath_)});
}
```

- `buildExportRequest()`: replace the three `exportFormat_/jpegQuality_/exportDpi_` lines with:

```cpp
    request.write_report = false;   // the desktop keeps no JSON report beside photos
    if (exportPrefs_) {
        request.export_format = exportPrefs_->format().toStdString();
        request.jpeg_quality = exportPrefs_->jpegQuality();
        request.export_dpi = exportPrefs_->dpi();
    }
    if (lightroomRoundTrip_) {
        request.export_format = "tiff";
        request.output_path = std::filesystem::path(currentFile_.toStdString());
    } else if (!pendingExportPath_.isEmpty()) {
        request.output_path = std::filesystem::path(QDir::toNativeSeparators(pendingExportPath_).toStdWString());
    }
```

- `exportImage()`: after `if (exporting_) return;` and before `endPeek();`, insert:

```cpp
    pendingExportPath_.clear();
    if (!lightroomRoundTrip_) {
        const QVariantMap target = exportTarget();
        if (target.value(QStringLiteral("skip")).toBool()) {
            status_ = QStringLiteral("Skipped: %1 already exists in %2")
                          .arg(target.value(QStringLiteral("fileName")).toString(),
                               QDir::toNativeSeparators(target.value(QStringLiteral("folder")).toString()));
            emit statusChanged();
            return;
        }
        const QString folder = target.value(QStringLiteral("folder")).toString();
        if (!QDir().mkpath(folder)) {
            status_ = QStringLiteral("Export failed: can't create the folder %1").arg(QDir::toNativeSeparators(folder));
            emit statusChanged();
            return;
        }
        pendingExportPath_ = target.value(QStringLiteral("path")).toString();
    }
```

- `onExportDone(msg)`: after `exporting_ = false; emit exportingChanged();` add:

```cpp
    if (!lightroomRoundTrip_ && msg.startsWith(QLatin1String("Exported:")) && !pendingExportPath_.isEmpty()) {
        lastExportPath_ = pendingExportPath_;
        emit lastExportPathChanged();
        if (exportPrefs_) exportPrefs_->noteExported(lastExportPath_);
    }
    pendingExportPath_.clear();
```

`main.cpp`: `#include "ExportPrefs.h"`; after `LibraryController library;`:

```cpp
    // Export choices (folder, name rule, format) are remembered; UI tests isolate them
    // in the DFEE_UI_SETTINGS ini like the QML settings.
    ExportPrefs exportPrefs(qEnvironmentVariable("DFEE_UI_SETTINGS"));
    controller.setExportPrefs(&exportPrefs);
```

after `setContextProperty("editStore", ...)`: `engine.rootContext()->setContextProperty("exportPrefs", &exportPrefs);`
In the selftest block replace `controller.setExportFormat(...)` with `exportPrefs.setFormat(qEnvironmentVariable("DFEE_SELFTEST_EXPORT_FORMAT"));` (the selftest lambda captures are unaffected).

`run_ui.ps1`: after the `not-an-image.arw` line add:

```powershell
# Exports default to a throwaway folder; "taken.jpg" lets scripts check the taken-name rules.
New-Item -ItemType Directory -Force (Join-Path $work "exports") | Out-Null
[IO.File]::WriteAllText((Join-Path $work "exports\taken.jpg"), "")
$env:DFEE_EXPORT_DIR = Join-Path $work "exports"
```

`FlExportSheet.qml` (this task only rebinds; Task 5 redesigns): `engine.exportFormat` → `exportPrefs.format`, `engine.jpegQuality` → `exportPrefs.jpegQuality`, `engine.exportDpi` → `exportPrefs.dpi` (all occurrences, including assignments). Replace the "Saves to … Next to the original" row with:

```qml
    Text {
        objectName: "exportNameExample"
        width: parent.width
        text: sheet.target.fileName || ""
        elide: Text.ElideMiddle
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Text {
        objectName: "exportTargetNote"
        width: parent.width
        visible: text.length > 0
        wrapMode: Text.Wrap
        text: !sheet.target.exists ? ""
            : sheet.target.skip ? "A file with this name exists, so it will be skipped."
            : sheet.target.replaces ? "Will replace the existing file."
            : "A file with this name exists, so a number is added."
        color: sheet.target.skip || sheet.target.replaces ? Theme.danger : Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
```

and add to the sheet root:

```qml
    // Re-read whenever anything that names the file changes, and on open (the
    // folder's contents may have changed since).
    property int refresh: 0
    onOpened: refresh++
    readonly property var target: {
        refresh; exportPrefs.folderPath; exportPrefs.nextToOriginal; exportPrefs.nameTemplate;
        exportPrefs.collision; exportPrefs.format; exportPrefs.sequence;
        engine.currentFile; engine.stock; engine.imageInfo;
        return engine.exportTarget();
    }
```

and `enabled: engine.hasImage && !engine.exporting && !sheet.target.skip` on `exportConfirm`.

`v2_sheets.script`: `engine.exportFormat` → `exportPrefs.format`, `engine.exportDpi` → `exportPrefs.dpi`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build desktop/out/build --config Release --target DFEE image_info_tests export_tests`, then `desktop/out/build/Release/image_info_tests.exe`, `desktop/out/build/Release/export_tests.exe`, and
`powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_export_target.script` and the same for `v2_sheets.script`.
Expected: unit tests all pass; both scripts end `UISCRIPT DONE failures=0`.
Then the export-peek regression (refused export, no file written): run `v2_export_peek.script` through its Lightroom wrapper with `$env:DFEE_NATIVE_EXPORT_MEMORY_BUDGET_MB=1` exactly as before → `failures=0`.

- [ ] **Step 5: Commit**

```bash
git add desktop/src/ImageInfo.cpp desktop/tests/image_info_test.cpp desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/src/main.cpp desktop/tests/run_ui.ps1 desktop/qml/v2/dialogs/FlExportSheet.qml desktop/tests/ui/v2_sheets.script desktop/tests/ui/v2_export_target.script
git commit -m "feat(desktop): exports go to the remembered folder under the template name; capture date"
```

---

### Task 5: The export sheet — folder picker, favourites, name field, rules; Show in folder

**Files:**
- Modify: `desktop/qml/v2/dialogs/FlExportSheet.qml` (rewrite the top half: Save to / File name / If the name exists)
- Modify: `desktop/qml/v2/controls/FlListPopup.qml` (`focusReturn`)
- Modify: `desktop/qml/v2/canvas/FlCanvas.qml` (status bar "Show in folder")
- Create: `desktop/resources/icons/star.svg`, `desktop/resources/icons/star-fill.svg`
- Test: `desktop/tests/ui/v2_export_sheet.script`, `desktop/tests/ui/v2_export_persist_a.script`, `desktop/tests/ui/v2_export_persist_b.script`

**Interfaces:**
- Consumes: `exportPrefs.*` (Task 3), `engine.exportTarget()`, `engine.lastExportPath`, `engine.showLastExport()` (Task 4).
- Produces: objectNames `exportFolderBox`, `exportFolderLabel`, `exportFolderPicker`, rows `exportFolder_default` / `exportFolder_next` / `exportFolder_choose` / `exportFolder_path:<folder>`, `exportFavoriteButton`, `exportNameField`, `exportToken_name|film|date|seq|camera`, `exportCollisionSegmented`, `showInFolderButton` (plus Task 4's `exportNameExample`, `exportTargetNote`, `exportConfirm`).

- [ ] **Step 1: Write the failing tests**

`desktop/tests/ui/v2_export_sheet.script`:

```
# Export sheet: pick folders from the dropdown, favourite one, insert name tokens,
# choose the taken-name rule; Esc still closes the sheet after using the picker.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
key:83+ctrl
wait:300
expect:exportSheet.visible=true
click:exportFolderBox@0.5,0.5
wait:300
expect:exportFolderPicker.visible=true
click:exportFolder_next@0.5,0.5
wait:300
expect:exportFolderPicker.visible=false
expect:exportPrefs.nextToOriginal=true
expect:exportFolderLabel.text=Next to the original
expect:exportFavoriteButton.visible=false
click:exportFolderBox@0.5,0.5
wait:300
click:exportFolder_default@0.5,0.5
wait:300
expect:exportPrefs.nextToOriginal=false
expect:exportFavoriteButton.visible=true
click:exportFavoriteButton@0.5,0.5
wait:200
expect:exportPrefs.favorites.length=1
expect:exportFavoriteButton.iconName=star-fill
click:exportNameField@0.5,0.5
key:16777233
click:exportToken_seq@0.5,0.5
wait:200
expect:exportPrefs.nameTemplate={name}_{film}{seq}
expect:exportNameExample.text=3071874357_0001.jpg
click:exportCollisionSegmented@0.85,0.5
wait:200
expect:exportPrefs.collision=skip
click:exportCollisionSegmented@0.15,0.5
wait:200
expect:exportPrefs.collision=number
key:16777216
wait:300
expect:exportSheet.visible=false
quit
```

(`{name}_{film}{seq}` with no film expands to `3071874357_0001`.)

`v2_export_persist_a.script`:

```
# Run a then b with the same -UiSettings ini: export choices survive a restart.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
set:exportPrefs.nameTemplate={date}_{name}
set:exportPrefs.collision=skip
set:exportPrefs.format=tiff
set:exportPrefs.dpi=600
key:83+ctrl
wait:300
click:exportFavoriteButton@0.5,0.5
wait:200
expect:exportPrefs.favorites.length=1
quit
```

`v2_export_persist_b.script`:

```
expect:exportPrefs.nameTemplate={date}_{name}
expect:exportPrefs.collision=skip
expect:exportPrefs.format=tiff
expect:exportPrefs.dpi=600
expect:exportPrefs.favorites.length=1
quit
```

- [ ] **Step 2: Run to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_export_sheet.script`
Expected: `UISCRIPT FAIL click:exportFolderBox...` (object not found).
(The persist pair passes already for the `set:` lines but fails on `exportFavoriteButton` not found.)

- [ ] **Step 3: Implement**

Icons (Phosphor, MIT):

```bash
curl -sL https://raw.githubusercontent.com/phosphor-icons/core/main/assets/regular/star.svg | sed 's/currentColor/#ffffff/g' > desktop/resources/icons/star.svg
curl -sL https://raw.githubusercontent.com/phosphor-icons/core/main/assets/fill/star-fill.svg | sed 's/currentColor/#ffffff/g' > desktop/resources/icons/star-fill.svg
```

Check each file contains `fill="#ffffff"` on the root `<svg>` (add it if Phosphor's file has no fill attribute) — same form as `sidebar-simple-fill.svg`.

`FlListPopup.qml`: add `property Item focusReturn: null   // inside a sheet: give focus back there, not to the window` and change `onClosed` to:

```qml
    onClosed: Qt.callLater(function() {
        if (pop.focusReturn) { pop.focusReturn.forceActiveFocus(); return; }
        const w = pop.parent ? pop.parent.Window.window : null;
        if (w && w.returnFocus) w.returnFocus();
    })
```

`FlExportSheet.qml` — header comment becomes:

```qml
// Export: where the file goes (remembered folder, favourites, recents, or next to the
// original), its name (a template with a live example), what happens when the name is
// taken, and the format. Lightroom mode never opens this sheet; it saves straight back.
```

`width: 520`. Add `import QtQuick.Dialogs`. Add helpers to the root:

```qml
    readonly property int labelWidth: 112
    function folderName(p) { const parts = p.split("/"); return parts[parts.length - 1] || p; }
    function shortPath(p) {
        const parts = p.split("/");
        return parts.length > 3 ? "…/" + parts.slice(-2).join("/") : p;
    }
    readonly property var folderRows: {
        const rows = [];
        for (const p of exportPrefs.favorites)
            rows.push({ id: "path:" + p, name: folderName(p), detail: p, group: "Favourites" });
        for (const p of exportPrefs.recents)
            if (!exportPrefs.isFavorite(p))
                rows.push({ id: "path:" + p, name: folderName(p), detail: p, group: "Recent" });
        rows.push({ id: "default", name: "Film Lab Exports", detail: exportPrefs.defaultFolder, group: "Folders" });
        rows.push({ id: "next", name: "Next to the original", detail: "The photo's own folder", group: "Folders" });
        rows.push({ id: "choose", name: "Choose folder…", detail: "", group: "Folders" });
        return rows;
    }
    function pickFolder(id) {
        if (id === "choose") folderDialog.open();
        else if (id === "next") exportPrefs.useNextToOriginal();
        else if (id === "default") exportPrefs.useDefaultFolder();
        else if (id.startsWith("path:")) exportPrefs.useFolder(id.slice(5));
    }
    FolderDialog {
        id: folderDialog
        title: "Export to folder"
        currentFolder: "file:///" + exportPrefs.folderPath
        onAccepted: exportPrefs.useFolderUrl(selectedFolder)
    }
```

Before the Format row, add the three rows:

```qml
    Item {                                   // Save to: folder dropdown + favourite star
        width: parent.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Save to"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Rectangle {
            id: folderBox
            objectName: "exportFolderBox"
            x: sheet.labelWidth
            width: parent.width - sheet.labelWidth - favButton.width - 6
            height: Theme.controlHeight
            radius: Theme.radiusControl
            color: Theme.inset
            FlIcon {
                id: folderIcon
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "folder-simple"
                size: 14
                color: Theme.textSecondary
            }
            Text {
                objectName: "exportFolderLabel"
                anchors.left: folderIcon.right
                anchors.leftMargin: 6
                anchors.right: caret.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: exportPrefs.nextToOriginal ? "Next to the original" : sheet.shortPath(exportPrefs.folderPath)
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: Theme.fontLabel
            }
            FlIcon {
                id: caret
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "caret-down"
                size: 12
                color: Theme.textTertiary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: folderPicker.visible ? folderPicker.close() : folderPicker.open()
            }
            FlListPopup {
                id: folderPicker
                objectName: "exportFolderPicker"
                y: parent.height + 4
                width: parent.width
                rowPrefix: "exportFolder_"
                rows: sheet.folderRows
                currentId: exportPrefs.nextToOriginal ? "next" : "path:" + exportPrefs.folderPath
                focusReturn: sheet.contentItem
                onPicked: (id) => sheet.pickFolder(id)
            }
        }
        FlIconButton {
            id: favButton
            objectName: "exportFavoriteButton"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !exportPrefs.nextToOriginal
            readonly property bool favorite: { exportPrefs.favorites; return exportPrefs.isFavorite(exportPrefs.folderPath); }
            iconName: favorite ? "star-fill" : "star"
            tip: favorite ? "Remove from favourites" : "Add to favourites"
            onClicked: exportPrefs.toggleFavorite(exportPrefs.folderPath)
        }
    }
    Item {                                   // File name: template field
        width: parent.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "File name"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlTextField {
            id: nameField
            objectName: "exportNameField"
            x: sheet.labelWidth
            width: parent.width - sheet.labelWidth
            tabStop: true
            escReturnsFocus: false
            text: exportPrefs.nameTemplate
            onTextEdited: exportPrefs.nameTemplate = text
        }
    }
    Row {                                    // tokens insert at the cursor
        x: sheet.labelWidth
        spacing: 6
        Repeater {
            model: ["{name}", "{film}", "{date}", "{seq}", "{camera}"]
            delegate: Rectangle {
                objectName: "exportToken_" + modelData.slice(1, -1)
                width: tokenText.implicitWidth + 14
                height: 22
                radius: Theme.radiusControl
                color: tokenArea.containsMouse ? Theme.rowSelected : Theme.inset
                Text {
                    id: tokenText
                    anchors.centerIn: parent
                    text: modelData
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
                MouseArea {
                    id: tokenArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        nameField.insert(nameField.cursorPosition, modelData);
                        exportPrefs.nameTemplate = nameField.text;
                    }
                }
            }
        }
    }
```

Move Task 4's `exportNameExample` and `exportTargetNote` texts to sit right after the token row with `x: sheet.labelWidth; width: parent.width - sheet.labelWidth`, and add after them:

```qml
    Item {                                   // If the name exists
        width: parent.width
        height: Theme.segmentHeight + 4
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "If the name exists"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlSegmented {
            objectName: "exportCollisionSegmented"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            readonly property var rules: ["number", "replace", "skip"]
            model: ["Add number", "Replace", "Skip"]
            currentIndex: Math.max(0, rules.indexOf(exportPrefs.collision))
            onActivated: (i) => exportPrefs.collision = rules[i]
        }
    }
    FlHairline { width: parent.width }
```

`FlCanvas.qml` status bar: add inside `statusBar`, after `statusText`:

```qml
        FlButton {
            id: showInFolderButton
            objectName: "showInFolderButton"
            kind: "text"
            text: "Show in folder"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: engine.status.startsWith("Exported:") && engine.lastExportPath.length > 0
            onClicked: engine.showLastExport()
        }
```

and change `statusText`'s `anchors.rightMargin: 16` to `anchors.rightMargin: showInFolderButton.visible ? showInFolderButton.width + 28 : 16`.

If `FlSegmented` is too wide for the sheet at 520 px, keep the labels and let the sheet grow to 560 — check visually in the `shot:` step of Step 4.

- [ ] **Step 4: Run tests to verify they pass**

Run:
```
powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_export_sheet.script
$ini = "$env:TEMP\export-persist.ini"; Remove-Item $ini -ErrorAction SilentlyContinue
powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_export_persist_a.script -UiSettings $ini
powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_export_persist_b.script -UiSettings $ini
```
Expected: each `UISCRIPT DONE failures=0`. Add `shot:${TEMP}/v2_export_sheet.png` before `quit` in a scratch copy and look at it: rows aligned, nothing clipped, picker groups readable.

- [ ] **Step 5: Commit**

```bash
git add desktop/qml/v2/dialogs/FlExportSheet.qml desktop/qml/v2/controls/FlListPopup.qml desktop/qml/v2/canvas/FlCanvas.qml desktop/resources/icons/star.svg desktop/resources/icons/star-fill.svg desktop/tests/ui/v2_export_sheet.script desktop/tests/ui/v2_export_persist_a.script desktop/tests/ui/v2_export_persist_b.script
git commit -m "feat(desktop): export sheet picks remembered/favourite folders, name tokens, taken-name rule; Show in folder"
```

---

### Task 6: Full suite, docs, installer

**Files:**
- Modify: `desktop/DESIGN.md` (export sheet section: rows, folder picker groups, copy strings)
- Modify: `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (Export: point to the export spec)

- [ ] **Step 1: Run every suite**

Run (each must pass):
- `cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe`, `dfee_tests.exe`
- `desktop/out/build/Release/desktop_tests.exe`, `image_info_tests.exe`, `stock_catalog_tests.exe`, `export_tests.exe`
- every `desktop/tests/ui/*.script` via `run_ui.ps1` (gallery with `-Ui gallery`; Lightroom scripts through a wrapper `.ps1` with `-AppArgs @('--lightroom-edit', <temp copy of a TIFF>)`; `v2_panels_persist_*` and `v2_export_persist_*` pairs with a shared `-UiSettings`; `v2_minsize` with `-AppArgs --min-size`).
Expected: all `failures=0`.

- [ ] **Step 2: Docs** — in `desktop/DESIGN.md` add an "Export sheet" subsection listing: Save to (folder box + star; picker groups Favourites / Recent / Folders), File name (template field, token chips `{name} {film} {date} {seq} {camera}`, example line), If the name exists (Add number / Replace / Skip), Format rows; status bar "Show in folder". In the UI redesign spec's Export paragraph add: "Detailed in `2026-10-07-export-design.md`."

- [ ] **Step 3: Release + installer** — check no Film Lab is running (`tasklist | grep -i filmlab`), then:

```
cmake --build desktop/out/build --config Release --target DFEE
powershell -ExecutionPolicy Bypass -File desktop/packaging/deploy.ps1
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" desktop\packaging\FilmLab.iss
```
Expected: both exit 0.

- [ ] **Step 4: Commit**

```bash
git add desktop/DESIGN.md docs/superpowers/specs/2026-09-27-ui-redesign-design.md
git commit -m "docs: export sheet (destination, naming, taken-name rule)"
```

- [ ] **Step 5: Hand-off check (needs the user's OK — real export):** ask the user to export one photo with the default settings and confirm the file lands in `Pictures\Film Lab Exports` as `<name>_<film>.jpg`, "Show in folder" selects it, and a second export of the same photo becomes `-2`.
