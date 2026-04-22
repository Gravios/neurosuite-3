// param.h — parameter management (C file, needs C linkage when included from C++)
#pragma once
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum type_t {FLOAT = 'f', INT = 'd', BOOLEAN = 'b', STRING = 's'};

#define FLOAT_PARAM(name)   add_param(FLOAT,   #name, &name)
#define INT_PARAM(name)     add_param(INT,     #name, &name)
#define BOOLEAN_PARAM(name) add_param(BOOLEAN, #name, &name)
#define STRING_PARAM(name)  add_param(STRING,  #name, name)

#define STRLEN 10000

// Use extern "C" when included from C++ so the linker finds the C-compiled symbols.
#ifdef __cplusplus
extern "C" {
#endif

// name is a string literal produced by # stringification — must be const char*
void add_param(int t, const char *name, void *addr);
int  change_param(const char *name, const char *value);
void init_params(int argc, char **argv);
void print_params(FILE *fp);

#ifdef __cplusplus
}
#endif
