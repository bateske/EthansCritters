// Number formatting without printf: the SDK's snprintf pulls in ~3.5 KB
// (and has no %l). Each call writes at p, terminates the string and returns
// the new end, so calls chain:
//
//     char buf[16], *p = buf;
//     p = fmtStr(p, "BET "); p = fmtMoney(p, bet);
#pragma once
#include <stdint.h>

char *fmtInt(char *p, int32_t v);           // "-123"
char *fmtMoney(char *p, int32_t v);         // "$1234", "-$5"
char *fmtCash(char *p, int32_t v);          // "$1,234": money with thousands separators
char *fmtShort(char *p, int32_t v);         // as fmtMoney, but "$12K" from $10,000
char *fmtTime(char *p, uint16_t secs);      // "1:05"
char *fmtStr(char *p, const char *s);       // a copy of s
