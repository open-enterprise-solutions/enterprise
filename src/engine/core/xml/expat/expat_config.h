/* EXPAT, CONFIGURED THE WAY THIS BUILD NEEDS IT - one header for both build systems.
 *
 * Expat's own CMake would generate this file by probing the machine; this build compiles the submodule's C files
 * itself (the root CMakeLists.txt and expat.vcxproj beside this file), as it does llhttp and lodepng, so the probe's
 * answers are written here for the platforms the engine builds on. The submodule's tree is never written into.
 *
 * Entropy (the hash salt against crafted collisions): rand_s on Windows, arc4random_buf on macOS, getrandom then
 * /dev/urandom on Linux - each of those is a file of expat's (lib/random_*.c) the two builds add for that platform. */
#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H 1

#define BYTEORDER 1234   /* every target of the engine is little-endian: x86, x64, arm64 */

#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_INTTYPES_H 1
#define STDC_HEADERS 1

#if defined(_WIN32)
/* random_rand_s.c defines _CRT_RAND_S itself */
#elif defined(__APPLE__)
#  define HAVE_ARC4RANDOM_BUF 1
#  define HAVE_UNISTD_H 1
#  define HAVE_FCNTL_H 1
#else
#  define HAVE_GETRANDOM 1
#  define XML_DEV_URANDOM 1
#  define HAVE_UNISTD_H 1
#  define HAVE_FCNTL_H 1
#endif

#define PACKAGE "expat"
#define PACKAGE_NAME "expat"
#define PACKAGE_TARNAME "expat"
#define PACKAGE_URL ""

#define XML_CONTEXT_BYTES 1024
#define XML_DTD 1   /* parameter entities exist as a feature, so they can be switched off (XML_SetParamEntityParsing) */
#define XML_GE 1
#define XML_NS 1

#ifdef _MSC_VER
#  define __func__ __FUNCTION__
#endif

#endif /* EXPAT_CONFIG_H */
