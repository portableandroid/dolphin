
if(PORTANDROID)
  set(distribution_DIR ${CFG_LIBS_DIR})
  set(import_libs_DIR ${CFG_LIBS_DIR})
  set(main_entry_source ${CFG_ENTRY_DIR}/emu_${CFG_MODULE_NAME}_main.cpp)

  # add include directories
  include_directories(${import_libs_DIR}/classicboy/include
          ${import_libs_DIR}/libBlocksRuntime/include
          ${import_libs_DIR}/libcpufeatures/include
          ${import_libs_DIR}/libemu/include
          ${import_libs_DIR}/libemuRetro/include
          ${import_libs_DIR}/libfex/include
          ${import_libs_DIR}/libpng/include
          ${import_libs_DIR}/libzip/include)

  # added imported libraries
  add_library(lib_emu STATIC IMPORTED)
  set_target_properties(lib_emu PROPERTIES IMPORTED_LOCATION
          ${import_libs_DIR}/libemu/${CFG_BUILD_TYPE}/${ANDROID_ABI}/libemu.a)

  # added imported libraries
  add_library(lib_emuRetro STATIC IMPORTED)
  set_target_properties(lib_emuRetro PROPERTIES IMPORTED_LOCATION
          ${import_libs_DIR}/libemuRetro/${CFG_BUILD_TYPE}/${ANDROID_ABI}/libemuRetro.a)

endif()

add_library(dolphin_libretro SHARED
  Boot.cpp
  Host.cpp
  Input.cpp
  Log.cpp
  Main.cpp
  Options.cpp
  Video.cpp
  ${main_entry_source}
)

if (NOT APPLE)
   target_sources(dolphin_libretro PRIVATE
      Vulkan.cpp
   )
endif()

include_directories(${CMAKE_SOURCE_DIR}/Externals/Libretro/Include)
include_directories(${CMAKE_SOURCE_DIR}/Externals/Vulkan/Include)

set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}")

set_target_properties(dolphin_libretro PROPERTIES PREFIX "")
if(ANDROID)
   target_sources(dolphin_libretro PRIVATE
     ../../Android/jni/AndroidCommon/IDCache.cpp
   )
   if(NOT PORTANDROID)
      set_target_properties(dolphin_libretro PROPERTIES SUFFIX "_android.so")
   endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
   set(CLANG 1)
endif()

if(NOT MSVC AND NOT CLANG)
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
endif()

if (MSVC)
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DELAYLOAD:OpenGL32.dll /DELAYLOAD:DInput8.dll")
endif()

if(MSVC)
	if(LIBRETRO_STATIC)
		 target_link_libraries(dolphin_libretro PRIVATE
				Delayimp.lib
				core
				uicommon
				${LIBS}
				"-static-libgcc -static-libstdc++"
		 )
	else()
		 target_link_libraries(dolphin_libretro PRIVATE
				Delayimp.lib
				core
				uicommon
				${LIBS}
		 )
	endif()
else()
	if(LIBRETRO_STATIC)
		 target_link_libraries(dolphin_libretro PRIVATE
				core
				uicommon
				${LIBS}
				"-static-libgcc -static-libstdc++"
		 )
	else()

        if(PORTANDROID)
          target_link_libraries(dolphin_libretro PRIVATE
                  android
                  log
                  lib_emu
                  lib_emuRetro
                  core
                  uicommon
                  ${LIBS}
                  )

           set_target_properties(dolphin_libretro PROPERTIES OUTPUT_NAME "libcore_${CFG_MODULE_NAME}")
           set_target_properties(dolphin_libretro
                   PROPERTIES
                   LIBRARY_OUTPUT_DIRECTORY
                   "${distribution_DIR}/core_${CFG_MODULE_NAME}/${CFG_BUILD_TYPE}/${ANDROID_ABI}")

         else()
          target_link_libraries(dolphin_libretro PRIVATE
                  core
                  uicommon
                  ${LIBS}
                  )
         endif()

	endif()
endif()
