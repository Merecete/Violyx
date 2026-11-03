#pragma once
#include <stdint.h>
#include <stddef.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_LIMITS_H 1
#define LWIP_NO_UNISTD_H 1
typedef long ssize_t;
#define LWIP_NO_CTYPE_H 1
#define SSIZE_MAX 9223372036854775807L
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "lu"
void panic(const char*);
#define LWIP_PLATFORM_DIAG(x) do{}while(0)
#define LWIP_PLATFORM_ASSERT(x) panic(x)
unsigned int net_rand(void);
#define LWIP_RAND() ((u32_t)net_rand())
