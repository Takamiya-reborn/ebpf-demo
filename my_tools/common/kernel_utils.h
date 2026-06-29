// kernel_utils.h
#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>

static inline bool symbol_exists(const char *sym)
{
    FILE *f = fopen("/proc/kallsyms", "r");
    if (!f)
        return false;

    char addr[32];
    char type;
    char name[256];
    bool found = false;

    while (fscanf(f, "%31s %c %255s", addr, &type, name) == 3) {
        if (strcmp(name, sym) == 0) {
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

static inline char *get_kernel_release()
{
    struct utsname u;
    if (uname(&u) != 0)
        return NULL;
    return strdup(u.release);
}

#endif // KERNEL_UTILS_H
