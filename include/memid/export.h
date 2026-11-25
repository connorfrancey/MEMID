/* MEMID export and calling-convention macros. Plain C, so memid/c_api.h can use them.
 *
 *   MEMID_API   on every exported library symbol: dllexport while building the shared library (MEMID_BUILDING),
 *               dllimport when using it, nothing for the static library (MEMID_STATIC, set by the memid_static
 *               target). On ELF and Mach-O, default visibility.
 *   MEMID_CALL  the calling convention of the C ABI: __cdecl on 32-bit Windows (stated, so other compilers and
 *               P/Invoke agree), the platform default elsewhere.
 */
#ifndef MEMID_EXPORT_H
#define MEMID_EXPORT_H

#if defined(MEMID_STATIC)
#  define MEMID_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(MEMID_BUILDING)
#    define MEMID_API __declspec(dllexport)
#  else
#    define MEMID_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__)
#  define MEMID_API __attribute__((visibility("default")))
#else
#  define MEMID_API
#endif

#if defined(_WIN32) && !defined(_WIN64)
#  define MEMID_CALL __cdecl
#else
#  define MEMID_CALL
#endif

#endif
