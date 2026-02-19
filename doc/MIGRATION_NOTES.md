# Qt6 / Modern C++ Migration Notes

## libklustersshared-qt6

### CMakeLists.txt
- `cmake_minimum_required` bumped to `3.16`
- Qt4/Qt5 detection logic replaced with `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)`
- `CMAKE_CXX_STANDARD 17` set explicitly
- `CMAKE_AUTOMOC/AUTOUIC/AUTORCC ON` (replaces manual `qt4_wrap_cpp`)
- `qt4_add_resources` → `qt_add_resources`
- Custom `MacroWriteBasicCMakeVersionFile` replaced with CMake built-in `write_basic_package_version_file`
- Massive legacy GCC-version-detection block removed
- Proper `target_include_directories` with `PUBLIC` interface for installed headers

### Removed: libinqt5/
- `QStandardPaths` backport entirely removed — it's part of `QtCore` since Qt5.0

### src/shared/array.h — full C++17 rewrite
- Raw `T* array` → `std::unique_ptr<T[]>` (RAII, no manual `delete[]`)
- Copy constructor/assignment marked `delete` (no accidental O(n) copies)
- Move constructor/assignment defaulted
- `setSize()` uses `make_unique` (exception-safe)
- `operator()` returns references, marked `[[nodiscard]]`
- `fillWithZeros()` / `copyData()` retain performance via `memset`/`memcpy`
- `pArray<T>` similarly updated: owned raw pointers wrapped in `unique_ptr<T*[]>`

### src/gui/qhelpviewer — QWebView → QTextBrowser
- `QWebView` was removed in Qt6; replaced with `QTextBrowser`
- External URLs open via `QDesktopServices::openUrl`
- Internal navigation handled by `QTextBrowser::setSource`
- Connect updated: `SIGNAL/SLOT` macro → `&QTextBrowser::anchorClicked`

### Signal/slot connects — libklustersshared
- `qcolorbutton.cpp`: `SIGNAL(clicked())` → `&QPushButton::clicked`
- `qpagewidgetmodel.cpp`: item `changed`/`toggled` connects modernized
- `qpagewidget.cpp`: `currentPageChanged` connect uses lambda
- `qpageview.cpp`: `selectionChanged` uses new-style connect
- `qpageview_p.cpp`: `layoutChanged`, `currentChanged` connects modernized
- `qpagedialog_p.h`: connects use typed function pointers

### String API
- `QString::SkipEmptyParts` → `Qt::SkipEmptyParts` (moved enum in Qt6)

---

## neuroscope-qt6

### CMakeLists.txt
- Qt6 only, C++17, full AUTOMOC/AUTOUIC/AUTORCC
- `qt4_add_resources` / `qt4_wrap_ui` → `qt_add_resources` / `qt_wrap_ui`
- `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml)`
- WebKitWidgets dependency removed (not used directly by neuroscope)
- `configure_file` for `config-neuroscope.h` properly placed in `src/CMakeLists.txt`

### src/main.cpp — full modernization
- `QApplication::setGraphicsSystem("raster")` removed (no-op / removed in Qt5)
- Bespoke argument parser replaced with `QCommandLineParser`
- Proper `--help` and `--version` support via Qt framework

### src/eventsprovider.cpp
- `QRegExp` → `QRegularExpression` (QRegExp removed in Qt6)
- `#include <QRegularExpression>` added
- `qSort()` → `std::sort()` with iterators + `#include <algorithm>`

### src/itempalette.cpp
- `qSort(list)` → `std::sort(list.begin(), list.end())`
- `#include <algorithm>` added

### src/neuroscopedoc.cpp
- `#include <QRegExp>` removed (file uses only `QRegularExpression` patterns via QString)
- `QString::SkipEmptyParts` → `Qt::SkipEmptyParts`

### src/tracewidget.cpp / channelgroupview.cpp / itemgroupview.cpp
- `QLayout::setMargin(n)` removed in Qt6 → `setContentsMargins(n,n,n,n)`

### Signal/slot connects — neuroscope
- Old-style `SIGNAL/SLOT` macros are **deprecated** in Qt6 but not yet removed;
  they compile with `-Wno-deprecated-declarations`.
- Timer connect in `tracewidget.cpp` updated to typed pointer style.
- Full systematic replacement of all 343 SIGNAL/SLOT macros is recommended
  as a follow-up; use Qt Creator's "Convert connect() to new syntax" refactoring.

---

## Recommended follow-up work

1. **Complete signal/slot modernization** in neuroscope (343 macros). Use Qt Creator's
   built-in "Convert to new-style signal/slot" refactoring, or clang-tidy with
   `modernize-use-using` + `qt-keyword-replace`.

2. **Q_PRIVATE_SLOT** in qextenddialog.h — replace with private lambdas or
   move private implementations to named private methods.

3. **`QHash<QString, Foo*>` ownership** — consider replacing raw-pointer maps with
   `QHash<QString, std::unique_ptr<Foo>>` where ownership is unambiguous.
   Current `qDeleteAll()` calls remain safe but are a smell.

4. **Replace `long` with `qsizetype`/`std::ptrdiff_t`** where array indices are used
   to match Qt6's 64-bit-safe container API.

5. **Add `override` / `final`** to all virtual overrides (zero occurrences in original).

6. **`QFileInfo::isRelative()` path joining** — replace `path + "/" + name` pattern
   with `QDir::cleanPath(QDir(path).filePath(name))`.
