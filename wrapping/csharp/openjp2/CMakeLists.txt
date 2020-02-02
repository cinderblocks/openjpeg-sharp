#CSharpOpenJPEG/CMakeLists.txt

set(openjpeg-sharp_SRCS
  CSharpOpenJPEG.c
  ${OPENJPEG_SOURCE_DIR}/src/bin/common/opj_getopt.c
  )

add_library(openjpeg-sharp
  ${openjpeg-sharp_SRCS}
  )

target_include_directories(openjpeg-sharp PRIVATE
  ${OPENJPEG_BINARY_DIR}/src/lib/openjp2 # opj_config.h
  ${OPENJPEG_SOURCE_DIR}/src/lib/openjp2
  ${OPENJPEG_SOURCE_DIR}/src/bin/common
  )

target_link_libraries(openjpeg-sharp PRIVATE
  ${OPENJPEG_LIBRARY_NAME}
  ${PNG_LIBNAME} ${TIFF_LIBNAME} ${LCMS_LIBNAME} ${Z_LIBNAME}
  )

if(UNIX)
  target_link_libraries(openjpeg-sharp m)
endif(UNIX)

install(TARGETS openjpeg-sharp 
  DESTINATION ${OPENJPEG_INSTALL_SHARE_DIR} 
  COMPONENT CSharpWrapper
  )
