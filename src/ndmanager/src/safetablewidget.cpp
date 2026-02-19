// safetablewidget.cpp — explicit translation unit so automoc generates
// the vtable and meta-object for SafeTableWidget. Header-only Q_OBJECT
// classes with no .cpp counterpart produce "undefined reference to vtable"
// at link time because the compiler emits the vtable in the first translation
// unit that includes the header, but automoc only runs on .cpp files.
#include "safetablewidget.h"
