#pragma once
#include <stddef.h>
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
int atoi(const char *); /* lwIP's netif.c uses it (GCC 14 refuses an undeclared call) */
