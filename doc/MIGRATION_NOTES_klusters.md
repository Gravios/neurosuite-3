# Klusters Qt6 / C++17 Migration Notes

## Summary

Migrated klusters-2.0.0 from Qt4/Qt5 to Qt6 with modern C++17 practices.

## Breaking Changes Fixed

### CMakeLists.txt (top-level + src/)
- `cmake_minimum_required` raised to 3.16
- `find_package(Qt6 ...)` replaces Qt4/Qt5 detection logic
- `qt_standard_project_setup()` added
- `qt4_add_resources` → `qt_add_resources`
- `qt4_wrap_ui` → `qt_wrap_ui`
- Removed 150+ lines of GCC version detection boilerplate
- Removed `ENFORCE_QT4_BUILD` option and Qt4/Qt5 conditional blocks
- `target_link_libraries` uses `Qt6::Core/Gui/Widgets/Xml`

### main.cpp
- `QApplication::setGraphicsSystem("raster")` removed (Qt6 drops raster system)
- Bespoke argument parser replaced with `QCommandLineParser`
- Added `#include <QCommandLineParser>` and `#include <QFileInfo>`

### Qt API replacements
| Old | New | Files |
|-----|-----|-------|
| `qSort(list)` | `std::sort(list.begin(), list.end())` | clustersprovider, correlationview, waveformview, data, klustersdoc, klusters, eventsprovider |
| `QString::SkipEmptyParts` | `Qt::SkipEmptyParts` | correlationview, klusters, data, klustersdoc |
| `QRegExp(...)` | `QRegularExpression(...)` | eventsprovider |
| `layout->setMargin(n)` | `layout->setContentsMargins(n,n,n,n)` | clusterinformationdialog, clusterPalette, tracewidget |
| `#include <qregexp.h>` | `#include <QRegularExpression>` | eventsprovider |

### Old-style Qt includes
All lowercase Qt4-era headers (e.g. `<qfile.h>`, `<qobject.h>`) updated to
modern Qt6 style (`<QFile>`, `<QObject>`) across all source files.
Note: libklustersshared headers (`qhelpviewer.h`, `qpagedialog.h`, etc.) remain
lowercase as they are project-internal headers, not Qt headers.

### `#include <algorithm>` additions
Added to all files using `std::sort` after the `qSort` replacement.

## Remaining Work (non-breaking in Qt6, but deprecated)

1. **Old-style SIGNAL/SLOT macros (~263 occurrences)** — still compile in Qt6
   with deprecation warnings. Should be migrated to typed function pointers
   or lambdas for compile-time safety.

2. **`Q_PRIVATE_SLOT` usage** — should be replaced with lambdas or private slots.

3. **Raw pointer member variables** — consider `std::unique_ptr` for owned objects.

4. **`long` used as array index** — consider `qsizetype` or `std::ptrdiff_t`.

5. **Missing `override`/`final`** — add to all virtual method overrides.

## Build Instructions

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires libklustersshared 2.0.0 (Qt6 build) to be installed or its
cmake config file to be on `CMAKE_PREFIX_PATH`.
