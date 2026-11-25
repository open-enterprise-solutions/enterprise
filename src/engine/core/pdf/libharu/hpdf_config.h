/* LIBHARU'S CONFIGURATION FOR THE MSBUILD BUILD (libharu.vcxproj) - what its CMake writes from
   include/hpdf_config.h.cmake for this tree: zlib, wx's copy, which the core links; no libpng - the core puts no
   PNG into a PDF. Its own win32/include/hpdf_config.h says libpng, so it is not the one taken. */

/* Define to 1 if you have the `z' library (-lz). */
#define LIBHPDF_HAVE_ZLIB
