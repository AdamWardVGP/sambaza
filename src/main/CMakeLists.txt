cmake_minimum_required(VERSION 3.1)

project(sambaza C)

if (MSVC)
    add_compile_options(/WX /W2)
else ()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif ()

if(NOT ANDROID)
    find_package(JNI REQUIRED)
endif()

find_package(PkgConfig REQUIRED)

# I believe these modules are located in
# Users/myuser/Libs/gstreamer-1.0-android-universal-<version>/<cpu_arch>/lib/gstreamer-1.0/pkgconfig
#message(FATAL_ERROR "${CMAKE_PREFIX_PATH}")
pkg_check_modules(GST REQUIRED #gstreamer-full-1.0
    gstreamer-1.0
    gstreamer-rtsp-1.0
    gstrtsp
    gstrtp
    gstrtpmanager
    gsttcp
    gstudp
    gstcoreelements
    gstapp
    gstlibav
    gstvideoconvertscale
    gstautodetect
    gstvideoparsersbad
    gstreamer-rtsp-server-1.0
    gstreamer-rtp-1.0
    gstreamer-sdp-1.0
    gstreamer-video-1.0
    gstreamer-app-1.0
    gstreamer-codecparsers-1.0
    orc-0.4
)

add_library(sambaza SHARED
        h265gstreamer.c)

# "resolve symbols locally within the shared object (.so) file, rather than looking for them globally."
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-Bsymbolic")

# generates position independent code (PIC) for shared libraries
target_compile_options(sambaza PRIVATE -fPIC)

message(STATUS "---adamtest123---")
 message(STATUS "GST_LIBRARIES: ${GST_LIBRARIES}")
 message(STATUS "GST_LINK_LIBRARIES: ${GST_LINK_LIBRARIES}")
 message(STATUS "GST_INCLUDE_DIRS: ${GST_INCLUDE_DIRS}")
 message(STATUS "ANDROID: ${ANDROID}")
 message(STATUS "JNI_INCLUDE_DIRS: ${JNI_INCLUDE_DIRS}")
 message(STATUS "JNI_LIBRARIES: ${JNI_LIBRARIES}")

target_include_directories(sambaza SYSTEM PRIVATE
        ${JNI_INCLUDE_DIRS}
        ${GST_INCLUDE_DIRS}
)

target_link_libraries(sambaza
        ${JNI_LIBRARIES}
        ${GST_LINK_LIBRARIES}
        android
)

#target_link_libraries(sambaza
##        gstrtsp
##        gstrtp
##        gstvideo-1.0
##        gstaudio-1.0
##        gstpbutils-1.0
##        gsttag-1.0
##        orc-0.4
##        gstrtpmanager
##        gsttcp
##        gstudp
##        gstcoreelements
##        gstrtspserver-1.0
##        gstbase-1.0
##        gstreamer-1.0
##        intl
##        gstrtsp-1.0
##        gstnet-1.0
##        gstsdp-1.0
##        gio-2.0
##        gstapp-1.0
##        gstrtp-1.0
##        gmodule-2.0
##        ffi
##        iconv
##        glib-2.0
##        gobject-2.0
##        z
##        gstbase-1.0
##        gstapp
##        gstvideoparsersbad
##        gstcodecparsers-1.0
##        gstvideo-1.0
##        log
#        )
