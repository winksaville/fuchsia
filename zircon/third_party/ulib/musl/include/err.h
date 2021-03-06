#ifndef SYSROOT_ERR_H_
#define SYSROOT_ERR_H_

#include <features.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void warn(const char*, ...);
void vwarn(const char*, va_list);
void warnx(const char*, ...);
void vwarnx(const char*, va_list);

_Noreturn void err(int, const char*, ...);
_Noreturn void verr(int, const char*, va_list);
_Noreturn void errx(int, const char*, ...);
_Noreturn void verrx(int, const char*, va_list);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_ERR_H_
