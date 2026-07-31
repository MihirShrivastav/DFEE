# DFEE Native Desktop — Phase 1 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A native Qt 6 `DFEE.exe` that links the C++ engine (`dfee_core`) and does open → pick film stock → live proxy preview → export, with no Python/React.

**Architecture:** A new self-contained `desktop/` Qt Quick executable links the existing `dfee_core` static library via `add_subdirectory(../cpp_engine)`. A C++ `EngineController` (`QObject`) owns one `dfee::EngineSession` and is exposed to QML; all engine calls run on a single dedicated worker thread (latest-wins coalescing) and the rendered preview is shown through a `QQuickImageProvider`.

**Tech Stack:** C++20, Qt 6 (Quick/Gui/Concurrent), CMake, MSVC 2022 x64, vcpkg (for the engine's OpenCV/LibRaw/yaml-cpp), the existing `dfee_core`.

## Global Constraints

- Language/std: **C++20** (`CMAKE_CXX_STANDARD 20`), matching the engine.
- Compiler/platform: **MSVC 2022, x64, Windows-first**.
- **Qt 6 dynamically linked under LGPLv3** — ship Qt DLLs alongside the exe; add an About/Licenses screen later (Phase 4). Do not statically link; do not use GPL-only Qt modules.
- **Reuse `dfee_core` unmodified.** No engine source changes in Phase 1.
- Engine dependencies come from **vcpkg** (`CMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`, triplet `x64-windows`); Qt comes from the Qt install (`CMAKE_PREFIX_PATH=<Qt>/msvc2022_64`).
- The engine build sets options `DFEE_BUILD_TESTS` and `DFEE_BUILD_PYTHON` (both default ON) — the desktop build **turns both OFF** so it never needs Python and doesn't build tests.
- Pipeline version: always pass **`effect_pipeline_version = "filmic_v3"`**.
- Everything lives under **`desktop/`**, separate from `cpp_engine/`, `frontend/`, and the Python code.
- Engine request/response types live in `cpp_engine/include/dfee/bridge_types.hpp`; the session API in `cpp_engine/include/dfee/session.hpp`.

---

## Prerequisites — Step 0 (performed by the user, once)

The agent cannot install Qt. Complete these before Task 1:

- [ ] **Install Qt 6** via the Qt Online Installer (https://www.qt.io/download-open-source). Select a Qt 6.5+ version → component **"MSVC 2022 64-bit"** and **"Qt Quick"**. Note the install path, e.g. `C:/Qt/6.8.1/msvc2022_64`.
- [ ] **Confirm the engine toolchain** already works (it does — the engine builds under `cpp_engine/out/build/windows-msvc-vcpkg`). Ensure `VCPKG_ROOT` is set and Ninja + the VS 2022 C++ toolset are on PATH (open a "x64 Native Tools Command Prompt for VS 2022", or use CMake's Ninja + auto-detected MSVC).
- [ ] **Verify Qt** is discoverable:

Run: `C:/Qt/6.8.1/msvc2022_64/bin/qmake.exe --version`
Expected: prints `QMake version ...` and `Using Qt version 6.x.x`.

Record two paths for later commands: `QT_DIR` (e.g. `C:/Qt/6.8.1/msvc2022_64`) and `VCPKG_TOOLCHAIN` (`%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`).

---

## Task 1: CMake project + blank Qt window linking the engine

Proves Qt is found, `dfee_core` links into a GUI executable, and a window renders.

**Files:**
- Create: `desktop/CMakeLists.txt`
- Create: `desktop/src/main.cpp`
- Create: `desktop/qml/Main.qml`

**Interfaces:**
- Produces: a CMake target `DFEE` (Qt Quick executable) linking `dfee_core`; a QML module URI `DFEE` with `Main.qml`.

- [ ] **Step 1: Write `desktop/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.26)
project(DFEEDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Build the engine core, but not its Python module or its tests.
set(DFEE_BUILD_TESTS OFF)
set(DFEE_BUILD_PYTHON OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cpp_engine ${CMAKE_BINARY_DIR}/cpp_engine)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Gui Concurrent)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(DFEE src/main.cpp)

qt_add_qml_module(DFEE
    URI DFEE
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(DFEE PRIVATE Qt6::Quick Qt6::Gui Qt6::Concurrent dfee_core)
set_target_properties(DFEE PROPERTIES WIN32_EXECUTABLE ON)
```

- [ ] **Step 2: Write `desktop/src/main.cpp`**

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("DFEE");
    app.setOrganizationName("DFEE");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DFEE", "Main");
    return app.exec();
}
```

- [ ] **Step 3: Write `desktop/qml/Main.qml`**

```qml
import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 1280
    height: 800
    visible: true
    title: "DFEE"
    color: "#0f0f10"

    Text {
        anchors.centerIn: parent
        text: "DFEE — native"
        color: "#c7c7cc"
        font.pixelSize: 20
    }
}
```

- [ ] **Step 4: Configure the build**

Run (from repo root; substitute your paths):
```
cmake -S desktop -B desktop/out/build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/msvc2022_64
```
Expected: configures without error; prints `DFEE: OpenCV enabled`, `DFEE: LibRaw enabled`, `DFEE: yaml-cpp enabled`, and finds Qt6.

- [ ] **Step 5: Build**

Run: `cmake --build desktop/out/build`
Expected: builds `dfee_core` then links `DFEE.exe` with no errors.

- [ ] **Step 6: Run and observe**

Run: `desktop/out/build/DFEE.exe` (Qt deploys its DLLs in-tree for a dev run; if it complains about a missing Qt DLL, run `C:/Qt/6.8.1/msvc2022_64/bin/windeployqt.exe desktop/out/build/DFEE.exe` once, then rerun).
Expected: a dark 1280×800 window titled "DFEE" with centered text "DFEE — native".

- [ ] **Step 7: Commit**

```bash
git add desktop/CMakeLists.txt desktop/src/main.cpp desktop/qml/Main.qml
git commit -m "desktop: Qt Quick window linking dfee_core (Phase 1 Task 1)"
```

---

## Task 2: EngineController + film-stock dropdown

Exposes the engine's stock list to QML in a dropdown.

**Files:**
- Create: `desktop/src/EngineController.h`
- Create: `desktop/src/EngineController.cpp`
- Modify: `desktop/CMakeLists.txt` (add the new sources + `qt_add_qml_module` type registration)
- Modify: `desktop/src/main.cpp` (instantiate controller, set as context property)
- Modify: `desktop/qml/Main.qml` (ComboBox bound to `stockNames`)

**Interfaces:**
- Produces: `class EngineController : QObject` with
  - `Q_PROPERTY(QStringList stockNames READ stockNames NOTIFY stocksChanged)`
  - `Q_PROPERTY(QString stock READ stock WRITE setStock NOTIFY stockChanged)` (holds the `stock_id`)
  - a `stockNames`/`stockIds` pair kept in parallel (display name ↔ id).
- Consumes: `dfee::EngineSession` (`list_profiles()`), `NativeProfilesResponse`, `NativeStockSummary{stock_id, stock_name, stock_type}` from `bridge_types.hpp`.

- [ ] **Step 1: Write `desktop/src/EngineController.h`**

```cpp
#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

namespace dfee { class EngineSession; }

class EngineController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList stockNames READ stockNames NOTIFY stocksChanged)
    Q_PROPERTY(QString stock READ stock WRITE setStock NOTIFY stockChanged)
public:
    explicit EngineController(QObject* parent = nullptr);
    ~EngineController() override;

    QStringList stockNames() const { return stockNames_; }
    QString stock() const { return stockId_; }
    void setStock(const QString& id);

signals:
    void stocksChanged();
    void stockChanged();

private:
    void loadStocks();

    std::unique_ptr<dfee::EngineSession> session_;
    QStringList stockNames_;   // display names, parallel to stockIds_
    QStringList stockIds_;     // stock_id values
    QString stockId_ = "none";
};
```

- [ ] **Step 2: Write `desktop/src/EngineController.cpp`**

```cpp
#include "EngineController.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QString>

EngineController::EngineController(QObject* parent)
    : QObject(parent), session_(std::make_unique<dfee::EngineSession>()) {
    loadStocks();
}

EngineController::~EngineController() = default;

void EngineController::loadStocks() {
    stockNames_.clear();
    stockIds_.clear();
    stockNames_ << "None";
    stockIds_ << "none";
    const dfee::NativeProfilesResponse profiles = session_->list_profiles();
    for (const auto& s : profiles.stocks) {
        stockNames_ << QString::fromStdString(s.stock_name);
        stockIds_ << QString::fromStdString(s.stock_id);
    }
    emit stocksChanged();
}

void EngineController::setStock(const QString& id) {
    if (stockId_ == id) return;
    stockId_ = id;
    emit stockChanged();
}
```

> Note: confirm the `dfee::EngineSession` constructor signature in `session.hpp`. If it takes a project-root path (it may, for locating `profiles/`), pass the repo root — define `DFEE_REPO_ROOT` for this target (see Step 3) and construct `dfee::EngineSession(DFEE_REPO_ROOT)`. `dfee_cli.cpp` and `test_core.cpp` show the exact construction to copy.

- [ ] **Step 3: Update `desktop/CMakeLists.txt`**

Replace the `qt_add_executable`/`qt_add_qml_module` block with:
```cmake
qt_add_executable(DFEE
    src/main.cpp
    src/EngineController.cpp
    src/EngineController.h
)

qt_add_qml_module(DFEE
    URI DFEE
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_compile_definitions(DFEE PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(DFEE PRIVATE Qt6::Quick Qt6::Gui Qt6::Concurrent dfee_core)
set_target_properties(DFEE PROPERTIES WIN32_EXECUTABLE ON)
```

- [ ] **Step 4: Update `desktop/src/main.cpp`** to expose the controller

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "EngineController.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("DFEE");
    app.setOrganizationName("DFEE");

    EngineController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("engine", &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DFEE", "Main");
    return app.exec();
}
```

- [ ] **Step 5: Update `desktop/qml/Main.qml`** with a stock dropdown

```qml
import QtQuick
import QtQuick.Window
import QtQuick.Controls

Window {
    width: 1280; height: 800; visible: true; title: "DFEE"; color: "#0f0f10"

    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        width: 300
        spacing: 12

        Text { text: "Film stock"; color: "#8b8b90"; font.pixelSize: 12 }
        ComboBox {
            id: stockBox
            width: parent.width
            model: engine.stockNames
            onActivated: engine.stock = engine.stockIdAt(currentIndex)
        }
    }
}
```

> `stockIdAt` is a tiny helper — add to `EngineController.h` as `Q_INVOKABLE QString stockIdAt(int i) const { return (i >= 0 && i < stockIds_.size()) ? stockIds_.at(i) : QString("none"); }` and rebuild.

- [ ] **Step 6: Build and run**

Run: `cmake --build desktop/out/build && desktop/out/build/DFEE.exe`
Expected: the window shows a "Film stock" dropdown listing "None" + all 33 stock names (Kodak Portra 400, …). Selecting one updates `engine.stock`.

- [ ] **Step 7: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/CMakeLists.txt desktop/src/main.cpp desktop/qml/Main.qml
git commit -m "desktop: EngineController + film-stock dropdown (Phase 1 Task 2)"
```

---

## Task 3: Open file → decode → film-rendered preview (worker thread)

Opens a TIFF/RAW, decodes it, renders a proxy preview through the engine, and shows it. Engine calls run on one worker thread; the result is delivered via a `QQuickImageProvider`.

**Files:**
- Create: `desktop/src/PreviewImageProvider.h`
- Modify: `desktop/src/EngineController.h` / `.cpp` (openFile, worker render, preview plumbing)
- Modify: `desktop/src/main.cpp` (register the image provider)
- Modify: `desktop/CMakeLists.txt` (add the provider header)
- Modify: `desktop/qml/Main.qml` (Open button + FileDialog + preview Image)

**Interfaces:**
- Produces:
  - `class PreviewImageProvider : QQuickImageProvider` with `void setImage(const QImage&)` (thread-safe via a `QMutex`) and `requestImage(...)` returning the latest frame.
  - `EngineController` gains: `Q_INVOKABLE void openFile(const QUrl&)`, `Q_PROPERTY(bool hasImage ...)`, `Q_PROPERTY(int previewRevision ...)` (bumped each new frame so QML can force-reload), `Q_PROPERTY(QString status ...)`.
- Consumes: `NativeSelectRequest{filename}`, `NativeRawDecodeRequest{filename, draft_mode=true}`, `NativePreviewRenderRequest{filename, stock, effect_pipeline_version="filmic_v3", film_exposure_ev, shadow_lift}`, `NativePreviewRenderResponse{status, jpeg_bytes}` from `bridge_types.hpp`.

- [ ] **Step 1: Write `desktop/src/PreviewImageProvider.h`**

```cpp
#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

class PreviewImageProvider : public QQuickImageProvider {
public:
    PreviewImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QImage& img) {
        QMutexLocker lock(&mutex_);
        image_ = img;
    }

    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        QMutexLocker lock(&mutex_);
        if (size) *size = image_.size();
        return image_;
    }
private:
    QMutex mutex_;
    QImage image_;
};
```

- [ ] **Step 2: Extend `EngineController.h`**

Add includes and members:
```cpp
#include <QUrl>
#include <QThread>
#include <QImage>

class PreviewImageProvider;

// inside class EngineController, add to Q_OBJECT properties:
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

// public:
    void setProvider(PreviewImageProvider* p) { provider_ = p; }
    Q_INVOKABLE void openFile(const QUrl& url);
    bool hasImage() const { return hasImage_; }
    int previewRevision() const { return previewRevision_; }
    QString status() const { return status_; }

// signals:
    void hasImageChanged();
    void previewChanged();
    void statusChanged();

// private:
    void scheduleRender();                 // coalesced kick to the worker
    void renderNow();                      // runs ON the worker thread
    QString currentFile_;
    PreviewImageProvider* provider_ = nullptr;
    QThread worker_;
    bool busy_ = false;
    bool dirty_ = false;
    bool hasImage_ = false;
    int previewRevision_ = 0;
    QString status_;
```

- [ ] **Step 3: Implement in `EngineController.cpp`**

```cpp
#include "PreviewImageProvider.h"
#include <QtConcurrent>
#include <QImage>
#include <QFileInfo>

// In the constructor, start the worker thread:
//   worker_.start();
// In the destructor: worker_.quit(); worker_.wait();

void EngineController::openFile(const QUrl& url) {
    currentFile_ = url.toLocalFile();
    status_ = "Loading " + QFileInfo(currentFile_).fileName();
    emit statusChanged();
    // Decode + first render on the worker thread.
    QMetaObject::invokeMethod(this, [this]() {
        try {
            dfee::NativeSelectRequest sel; sel.filename = currentFile_.toStdString();
            session_->select_file(sel);
            dfee::NativeRawDecodeRequest dec; dec.filename = currentFile_.toStdString();
            dec.draft_mode = true;
            session_->decode_raw(dec);
        } catch (const std::exception& e) {
            status_ = QString("Open failed: ") + e.what();
            QMetaObject::invokeMethod(this, [this]{ emit statusChanged(); }, Qt::QueuedConnection);
            return;
        }
        renderNow();
    }, Qt::QueuedConnection);
    // Note: to actually run on worker_, move a worker QObject to worker_; see Step 4.
}

void EngineController::renderNow() {
    if (currentFile_.isEmpty()) return;
    dfee::NativePreviewRenderRequest req;
    req.filename = currentFile_.toStdString();
    req.stock = stockId_.toStdString();
    req.effect_pipeline_version = "filmic_v3";
    req.film_exposure_ev = static_cast<float>(filmExposure_);
    req.shadow_lift = static_cast<float>(shadowLift_);
    dfee::NativePreviewRenderResponse resp;
    try {
        resp = session_->render_preview(req);
    } catch (const std::exception& e) {
        status_ = QString("Render failed: ") + e.what();
        QMetaObject::invokeMethod(this, [this]{ emit statusChanged(); }, Qt::QueuedConnection);
        return;
    }
    QImage img;
    img.loadFromData(resp.jpeg_bytes.data(),
                     static_cast<int>(resp.jpeg_bytes.size()), "JPG");
    if (provider_) provider_->setImage(img);
    // Marshal state changes back to the GUI thread.
    QMetaObject::invokeMethod(this, [this]() {
        hasImage_ = true; emit hasImageChanged();
        previewRevision_++; emit previewChanged();
        status_.clear(); emit statusChanged();
    }, Qt::QueuedConnection);
}
```

> Threading detail to implement cleanly: create a small `RenderWorker : QObject` (moved to `worker_`) that holds the `EngineSession` and exposes slots `openAndRender(QString file)` and `render(params)`; `EngineController` calls them via `QMetaObject::invokeMethod(worker_, ..., Qt::QueuedConnection)`. Coalesce with `busy_`/`dirty_`: if a render is requested while `busy_`, set `dirty_=true`; when a render finishes, if `dirty_`, kick again with the latest params. This keeps the `EngineSession` touched from exactly one thread and makes slider drags collapse to the latest frame. `filmExposure_`/`shadowLift_` are added in Task 4; declare them now defaulted to `0.0`.

- [ ] **Step 4: Register the provider in `main.cpp`**

```cpp
#include "PreviewImageProvider.h"
// ...
auto* provider = new PreviewImageProvider();      // owned by the engine below
controller.setProvider(provider);
engine.addImageProvider("preview", provider);      // QQmlApplicationEngine takes ownership
```

- [ ] **Step 5: Add the provider header to `CMakeLists.txt`** `qt_add_executable` sources list:
```cmake
    src/PreviewImageProvider.h
```

- [ ] **Step 6: Update `Main.qml`** with Open + preview

```qml
import QtQuick.Dialogs

// add at top of Window:
FileDialog {
    id: openDialog
    nameFilters: ["Images (*.tif *.tiff *.arw *.nef *.cr3 *.raf *.rw2 *.dng)"]
    onAccepted: engine.openFile(selectedFile)
}

// left canvas area:
Rectangle {
    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
    anchors.right: parent.right; anchors.rightMargin: 340
    color: "#0f0f10"
    Image {
        anchors.centerIn: parent
        width: parent.width - 40; height: parent.height - 40
        fillMode: Image.PreserveAspectFit
        cache: false
        source: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
        visible: engine.hasImage
    }
    Text {
        anchors.centerIn: parent; visible: !engine.hasImage
        text: "Open an image to begin"; color: "#5a5a60"; font.pixelSize: 15
    }
}

// add an Open button in the right column (above the stock dropdown):
Button { text: "Open…"; width: parent.width; onClicked: openDialog.open() }
```

- [ ] **Step 7: Build and run**

Run: `cmake --build desktop/out/build && desktop/out/build/DFEE.exe`
Then: Open a TIFF from `comparision/` or `raw_files/`.
Expected: the decoded, film-rendered (or neutral if stock=None) preview appears in the canvas within ~1 s; the UI stays responsive during decode.

- [ ] **Step 8: Commit**

```bash
git add desktop/src/PreviewImageProvider.h desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/src/main.cpp desktop/CMakeLists.txt desktop/qml/Main.qml
git commit -m "desktop: open/decode + film-rendered preview on worker thread (Phase 1 Task 3)"
```

---

## Task 4: Stock + two controls → live re-render

Wire the stock dropdown and two sliders (Film Exposure, Shadow Lift) to a coalesced re-render.

**Files:**
- Modify: `desktop/src/EngineController.h` / `.cpp` (add `filmExposure`, `shadowLift` properties + setters → `scheduleRender()`; `setStock` → `scheduleRender()`)
- Modify: `desktop/qml/Main.qml` (bind ComboBox + two Sliders)

**Interfaces:**
- Produces on `EngineController`:
  - `Q_PROPERTY(double filmExposure READ filmExposure WRITE setFilmExposure NOTIFY paramsChanged)` (range −5..+5 EV; 0 default)
  - `Q_PROPERTY(double shadowLift READ shadowLift WRITE setShadowLift NOTIFY paramsChanged)` (range −100..+100; 0 default)
  - `setStock`, `setFilmExposure`, `setShadowLift` each call `scheduleRender()`.

- [ ] **Step 1: Add properties to `EngineController.h`**

```cpp
    Q_PROPERTY(double filmExposure READ filmExposure WRITE setFilmExposure NOTIFY paramsChanged)
    Q_PROPERTY(double shadowLift READ shadowLift WRITE setShadowLift NOTIFY paramsChanged)
// public:
    double filmExposure() const { return filmExposure_; }
    double shadowLift() const { return shadowLift_; }
    void setFilmExposure(double v);
    void setShadowLift(double v);
// signals:
    void paramsChanged();
// private members already declared in Task 3:
//   double filmExposure_ = 0.0; double shadowLift_ = 0.0;
```

- [ ] **Step 2: Implement setters + make `setStock` re-render, in `EngineController.cpp`**

```cpp
void EngineController::setStock(const QString& id) {
    if (stockId_ == id) return;
    stockId_ = id;
    emit stockChanged();
    scheduleRender();
}

void EngineController::setFilmExposure(double v) {
    if (qFuzzyCompare(filmExposure_, v)) return;
    filmExposure_ = v; emit paramsChanged();
    scheduleRender();
}

void EngineController::setShadowLift(double v) {
    if (qFuzzyCompare(shadowLift_, v)) return;
    shadowLift_ = v; emit paramsChanged();
    scheduleRender();
}

void EngineController::scheduleRender() {
    if (currentFile_.isEmpty()) return;
    if (busy_) { dirty_ = true; return; }
    busy_ = true; dirty_ = false;
    // kick the worker; on completion set busy_=false and, if dirty_, call scheduleRender() again
    QMetaObject::invokeMethod(/* worker object */ this, [this]() {
        renderNow();
        QMetaObject::invokeMethod(this, [this]() {
            busy_ = false;
            if (dirty_) scheduleRender();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
```

- [ ] **Step 3: Bind controls in `Main.qml`** (in the right column, below the dropdown)

```qml
Text { text: "Film exposure"; color: "#8b8b90"; font.pixelSize: 12 }
Slider {
    width: parent.width; from: -5; to: 5; value: engine.filmExposure
    onMoved: engine.filmExposure = value
}
Text { text: "Shadow lift"; color: "#8b8b90"; font.pixelSize: 12 }
Slider {
    width: parent.width; from: -100; to: 100; value: engine.shadowLift
    onMoved: engine.shadowLift = value
}
```

- [ ] **Step 4: Build and run**

Run: `cmake --build desktop/out/build && desktop/out/build/DFEE.exe`
Then: open a file, pick a stock, drag each slider.
Expected: the preview updates live as you change the stock or drag sliders; rapid drags coalesce (no queue backlog / freeze).

- [ ] **Step 5: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/qml/Main.qml
git commit -m "desktop: live re-render on stock + film-exposure + shadow-lift (Phase 1 Task 4)"
```

---

## Task 5: Export to TIFF

Export the current edit to a 16-bit TIFF via the engine.

**Files:**
- Modify: `desktop/src/EngineController.h` / `.cpp` (add `Q_INVOKABLE void exportImage(const QUrl&)`)
- Modify: `desktop/qml/Main.qml` (Export button + save FileDialog)

**Interfaces:**
- Produces: `EngineController::exportImage(const QUrl&)` running `session_->export_image(NativeExportRequest)` on the worker thread; sets `status` to the resulting `output_path` or an error.
- Consumes: `NativeExportRequest : NativePreviewRenderRequest { export_format="tiff", ... }`, `NativeExportResponse{status, output_path}` from `bridge_types.hpp`.

- [ ] **Step 1: Declare in `EngineController.h`**

```cpp
    Q_INVOKABLE void exportImage(const QUrl& url);
```

- [ ] **Step 2: Implement in `EngineController.cpp`**

```cpp
#include <QFileInfo>

void EngineController::exportImage(const QUrl& url) {
    if (currentFile_.isEmpty()) return;
    const QString outPath = url.toLocalFile();
    QMetaObject::invokeMethod(/* worker object */ this, [this, outPath]() {
        dfee::NativeExportRequest req;
        req.filename = currentFile_.toStdString();
        req.stock = stockId_.toStdString();
        req.effect_pipeline_version = "filmic_v3";
        req.film_exposure_ev = static_cast<float>(filmExposure_);
        req.shadow_lift = static_cast<float>(shadowLift_);
        req.export_format = "tiff";
        QString msg;
        try {
            const dfee::NativeExportResponse resp = session_->export_image(req);
            msg = "Exported: " + QString::fromStdString(resp.output_path.string());
        } catch (const std::exception& e) {
            msg = QString("Export failed: ") + e.what();
        }
        QMetaObject::invokeMethod(this, [this, msg]() { status_ = msg; emit statusChanged(); },
                                  Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
```

> Note: `export_image` writes next to the source file by default in the engine/Python path; confirm the C++ `export_image` honors an output path or writes beside the source (check `session.cpp`). If it always writes beside the source, Phase 1 can drop the save dialog and just call `exportImage()` with no path, reporting `output_path` from the response. Keep the UI honest to whatever the engine does.

- [ ] **Step 3: Add Export UI to `Main.qml`**

```qml
FileDialog {
    id: saveDialog
    fileMode: FileDialog.SaveFile
    nameFilters: ["TIFF (*.tif)"]
    onAccepted: engine.exportImage(selectedFile)
}
Button {
    text: "Export…"; width: parent.width; enabled: engine.hasImage
    onClicked: saveDialog.open()
}
Text { text: engine.status; color: "#8b8b90"; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width }
```

- [ ] **Step 4: Build and run**

Run: `cmake --build desktop/out/build && desktop/out/build/DFEE.exe`
Then: open → pick stock → Export.
Expected: a `.tif` is written; the status line shows the output path; opening the TIFF shows the rendered result.

- [ ] **Step 5: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/qml/Main.qml
git commit -m "desktop: export to TIFF (Phase 1 Task 5)"
```

---

## Task 6: Graphite styling pass + manual smoke checklist

Give the window the Graphite look and document the manual smoke test.

**Files:**
- Modify: `desktop/qml/Main.qml` (dark palette, spacing, sentence-case labels)
- Create: `desktop/qml/Theme.qml` (a singleton-ish set of colors) — optional but recommended
- Create: `desktop/SMOKE.md`

**Interfaces:** none new — styling only.

- [ ] **Step 1: Add a `Theme` with Graphite tokens** (create `desktop/qml/Theme.qml`, register as a singleton via `qt_add_qml_module(... QML_FILES ... )` with `pragma Singleton` + a `qmldir` entry, or simplest: inline the colors as `readonly property` in `Main.qml`):

```qml
// Inline approach in Main.qml root:
readonly property color bg: "#0f0f10"
readonly property color card: "#1a1a1c"
readonly property color hair: "#ffffff14"   // ~0.08 alpha
readonly property color textPrimary: "#c7c7cc"
readonly property color textSecondary: "#8b8b90"
```

- [ ] **Step 2: Apply the palette** — set the right column inside a card `Rectangle { color: root.card; radius: 16; border.color: root.hair }`, labels use `root.textSecondary`, values/headings `root.textPrimary`, 18px padding, 14px spacing. Keep the canvas `root.bg`. Use sentence case for all labels ("Film stock", "Film exposure", "Shadow lift", "Open…", "Export…").

- [ ] **Step 3: Write `desktop/SMOKE.md`**

```markdown
# DFEE Desktop — Phase 1 smoke test

1. Launch DFEE.exe → dark Graphite window appears.
2. Open… → pick a TIFF from comparision/ → preview shows within ~1s.
3. Film stock → select "Kodak Portra 400" → preview changes.
4. Drag Film exposure and Shadow lift → preview updates live, no freeze.
5. Export… → choose a path → status shows "Exported: <path>"; the TIFF opens and matches the preview.
6. Open a RAW (.ARW) → decodes and previews.
```

- [ ] **Step 4: Build, run, walk the checklist**

Run: `cmake --build desktop/out/build && desktop/out/build/DFEE.exe`
Expected: the app matches the Graphite look and passes every SMOKE.md step.

- [ ] **Step 5: Commit**

```bash
git add desktop/qml/Main.qml desktop/qml/Theme.qml desktop/SMOKE.md
git commit -m "desktop: Graphite styling + smoke checklist (Phase 1 Task 6)"
```

---

## Self-review

- **Spec coverage:** open (T3) · stock list + pick (T2, T4) · two controls (T4) · live proxy preview off UI thread (T3, T4) · export TIFF (T5) · Graphite look (T6) · reuse dfee_core / no Python (T1 CMake) · worker-thread coalescing (T3/T4 notes) · error banners (T3/T5 status) · build/prereqs (Step 0, T1) — all covered.
- **Placeholder scan:** engine request/response field names are concrete (`filename`, `stock`, `effect_pipeline_version`, `film_exposure_ev`, `shadow_lift`, `export_format`, `jpeg_bytes`, `output_path`). Two flagged verifications (EngineSession ctor signature; whether `export_image` takes an output path) are explicit "confirm against `session.cpp`/`dfee_cli.cpp`" checks, not vague TODOs.
- **Type consistency:** `stockId_`/`stockNames_`/`stockIds_`, `filmExposure_`/`shadowLift_`, `previewRevision_`, `provider_`, and the `busy_`/`dirty_` coalescer are used consistently across T2–T5.
- **Threading:** the one non-trivial area — the plan repeatedly points at a `RenderWorker` moved to `worker_` so `EngineSession` is only ever touched from one thread; the executor should implement that worker object rather than the placeholder `invokeMethod(this, ...)` shown inline.
