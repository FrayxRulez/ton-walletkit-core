#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# libtwk_jni.so — the core plus its JNI shim, in one library.
#
# One file per ABI is what an app wants to ship, so the core is linked in
# statically rather than shipped beside this: System.loadLibrary("twk_jni") and
# nothing else.
#
# Usable two ways:
#   - Android Gradle: externalNativeBuild { cmake { path } } on this file, or
#     add_subdirectory() from an app's own jni/CMakeLists.txt (Telegram Android
#     builds its natives that way);
#   - desktop: the root build adds it whenever a JDK is found, so the JNI layer
#     can be tested on a normal JVM without an emulator.

add_library(twk_jni SHARED twk_jni.cpp)

target_link_libraries(twk_jni PRIVATE twk)

if(ANDROID)
    # The NDK sysroot provides jni.h; log for __android_log_write if we ever want it.
    target_link_libraries(twk_jni PRIVATE log)
else()
    find_package(JNI REQUIRED)
    target_include_directories(twk_jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()

set_target_properties(twk_jni PROPERTIES
    # A JVM looks for libtwk_jni.so / twk_jni.dll, so keep the name exact.
    OUTPUT_NAME twk_jni
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(ANDROID)
    # Android 15 devices may use 16 KB pages; a library aligned to 4 KB will not
    # load there. Costs nothing on older devices.
    target_link_options(twk_jni PRIVATE "-Wl,-z,max-page-size=16384")
endif()
