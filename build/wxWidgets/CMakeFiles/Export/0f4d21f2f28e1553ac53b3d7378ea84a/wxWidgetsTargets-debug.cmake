#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "wx::wxbase" for configuration "Debug"
set_property(TARGET wx::wxbase APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxbase PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_baseu-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_baseu-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxbase )
list(APPEND _cmake_import_check_files_for_wx::wxbase "${_IMPORT_PREFIX}/lib/libwx_baseu-3.3.2.0.0.dylib" )

# Import target "wx::wxnet" for configuration "Debug"
set_property(TARGET wx::wxnet APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxnet PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_baseu_net-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_baseu_net-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxnet )
list(APPEND _cmake_import_check_files_for_wx::wxnet "${_IMPORT_PREFIX}/lib/libwx_baseu_net-3.3.2.0.0.dylib" )

# Import target "wx::wxcore" for configuration "Debug"
set_property(TARGET wx::wxcore APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxcore PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_core-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_core-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxcore )
list(APPEND _cmake_import_check_files_for_wx::wxcore "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_core-3.3.2.0.0.dylib" )

# Import target "wx::wxadv" for configuration "Debug"
set_property(TARGET wx::wxadv APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxadv PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_adv-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_adv-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxadv )
list(APPEND _cmake_import_check_files_for_wx::wxadv "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_adv-3.3.2.0.0.dylib" )

# Import target "wx::wxaui" for configuration "Debug"
set_property(TARGET wx::wxaui APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxaui PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_aui-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_aui-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxaui )
list(APPEND _cmake_import_check_files_for_wx::wxaui "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_aui-3.3.2.0.0.dylib" )

# Import target "wx::wxhtml" for configuration "Debug"
set_property(TARGET wx::wxhtml APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxhtml PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_html-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_html-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxhtml )
list(APPEND _cmake_import_check_files_for_wx::wxhtml "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_html-3.3.2.0.0.dylib" )

# Import target "wx::wxpropgrid" for configuration "Debug"
set_property(TARGET wx::wxpropgrid APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxpropgrid PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_propgrid-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_propgrid-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxpropgrid )
list(APPEND _cmake_import_check_files_for_wx::wxpropgrid "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_propgrid-3.3.2.0.0.dylib" )

# Import target "wx::wxribbon" for configuration "Debug"
set_property(TARGET wx::wxribbon APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxribbon PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_ribbon-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_ribbon-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxribbon )
list(APPEND _cmake_import_check_files_for_wx::wxribbon "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_ribbon-3.3.2.0.0.dylib" )

# Import target "wx::wxrichtext" for configuration "Debug"
set_property(TARGET wx::wxrichtext APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxrichtext PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "wx::wxhtml;wx::wxxml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_richtext-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_richtext-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxrichtext )
list(APPEND _cmake_import_check_files_for_wx::wxrichtext "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_richtext-3.3.2.0.0.dylib" )

# Import target "wx::wxwebview" for configuration "Debug"
set_property(TARGET wx::wxwebview APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxwebview PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_webview-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_webview-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxwebview )
list(APPEND _cmake_import_check_files_for_wx::wxwebview "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_webview-3.3.2.0.0.dylib" )

# Import target "wx::wxstc" for configuration "Debug"
set_property(TARGET wx::wxstc APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxstc PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_stc-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_stc-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxstc )
list(APPEND _cmake_import_check_files_for_wx::wxstc "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_stc-3.3.2.0.0.dylib" )

# Import target "wx::wxxrc" for configuration "Debug"
set_property(TARGET wx::wxxrc APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxxrc PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "wx::wxhtml;wx::wxxml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_xrc-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_xrc-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxxrc )
list(APPEND _cmake_import_check_files_for_wx::wxxrc "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_xrc-3.3.2.0.0.dylib" )

# Import target "wx::wxmedia" for configuration "Debug"
set_property(TARGET wx::wxmedia APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxmedia PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_media-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_media-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxmedia )
list(APPEND _cmake_import_check_files_for_wx::wxmedia "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_media-3.3.2.0.0.dylib" )

# Import target "wx::wxgl" for configuration "Debug"
set_property(TARGET wx::wxgl APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxgl PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_gl-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_gl-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxgl )
list(APPEND _cmake_import_check_files_for_wx::wxgl "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_gl-3.3.2.0.0.dylib" )

# Import target "wx::wxqa" for configuration "Debug"
set_property(TARGET wx::wxqa APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxqa PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_qa-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_osx_cocoau_qa-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxqa )
list(APPEND _cmake_import_check_files_for_wx::wxqa "${_IMPORT_PREFIX}/lib/libwx_osx_cocoau_qa-3.3.2.0.0.dylib" )

# Import target "wx::wxxml" for configuration "Debug"
set_property(TARGET wx::wxxml APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(wx::wxxml PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libwx_baseu_xml-3.3.2.0.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libwx_baseu_xml-3.3.2.dylib"
  )

list(APPEND _cmake_import_check_targets wx::wxxml )
list(APPEND _cmake_import_check_files_for_wx::wxxml "${_IMPORT_PREFIX}/lib/libwx_baseu_xml-3.3.2.0.0.dylib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
