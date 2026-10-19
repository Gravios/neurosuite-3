// strsep.hpp — strsep() for every platform.
//
// strsep() is a BSD / glibc extension (string.h); MSVC does not have it, nor
// glibc's strdupa().  The processing programs split their "0,1,2:4,5,6"
// channel and threshold lists with them.  neurosuite::core::strsep has the
// BSD semantics exactly: returns the token at *stringp, terminates it at the
// first delimiter and advances *stringp past it, or sets *stringp to null at
// the end; empty tokens are returned as "".
//
// For strdupa(s) -- a stack copy that lives until the function returns --
// use a local std::string copy and its data() for the same lifetime.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEUROSUITE_CORE_STRSEP_HPP
#define NEUROSUITE_CORE_STRSEP_HPP

#include <cstring>

namespace neurosuite {
namespace core {

inline char* strsep(char** stringp, const char* delim)
{
    char* s = *stringp;
    if (!s) return nullptr;
    char* end = s + std::strcspn(s, delim);
    if (*end) { *end = '\0'; *stringp = end + 1; }
    else      { *stringp = nullptr; }
    return s;
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_STRSEP_HPP
