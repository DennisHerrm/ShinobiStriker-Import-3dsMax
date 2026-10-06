cmake_minimum_required(VERSION 3.20)
project(ShinobiStrikerImport LANGUAGES C CXX)

# ------------------------------------------------------------
#  Aufbau wie beim SWBF2/TFU2 Import: EIN Leser, zwei Ziele.
#    nsdump.exe     braucht KEIN Max-SDK  -> Gegenprobe
#    NSImport.dlu   braucht das Max-SDK   -> das Plugin
# ------------------------------------------------------------

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MAX_VERSION "0" CACHE STRING "3ds Max Jahrgang, z.B. 2026")
set(3DSMAX_SDK_DIR "" CACHE PATH "3ds Max SDK (maxsdk-Ordner)")
option(NS_BUILD_TOOL   "nsdump.exe bauen" ON)
option(NS_BUILD_PLUGIN "Max-Plugin bauen (braucht das Max SDK)" OFF)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "32-Bit-Bau. Es braucht x64:  cmake -S . -B build -A x64")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
if(MSVC)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
    add_compile_definitions(_MSVC_STL_HARDENING=1 _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

set(NS_LESER
    src/ns_io.h src/ns_io.cpp
    src/ns_paket.h src/ns_paket.cpp
    src/ns_bp.h src/ns_bp.cpp
    src/ns_mesh.h src/ns_mesh.cpp
    src/ns_anim.h src/ns_anim.cpp
    src/ns_tex.h src/ns_tex.cpp
    src/ns_katalog.h src/ns_katalog.cpp
    src/ns_figur.h src/ns_figur.cpp
    vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c)

add_library(nsleser STATIC ${NS_LESER})
target_include_directories(nsleser PUBLIC src vendor/miniz vendor/bcdec)
target_compile_definitions(nsleser PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
    target_compile_options(nsleser PRIVATE /EHsc /utf-8 /W4)
    set_source_files_properties(vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c
        vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c PROPERTIES COMPILE_FLAGS "/W0")
endif()

if(NS_BUILD_TOOL)
    add_executable(nsdump tools/nsdump.cpp)
    target_link_libraries(nsdump PRIVATE nsleser)
    target_compile_definitions(nsdump PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(nsdump PRIVATE /EHsc /utf-8 /W4)
    endif()
    set_target_properties(nsdump PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
endif()

if(NS_BUILD_PLUGIN)
    if(NOT EXISTS "${3DSMAX_SDK_DIR}/include/max.h")
        message(FATAL_ERROR "3ds Max SDK nicht gefunden unter: ${3DSMAX_SDK_DIR}")
    endif()
    add_library(NSImport SHARED
        src/nsimport.h
        src/nsimport_dll.cpp
        src/nsimport_max.cpp
        src/nsimport_fenster.cpp
        src/nsimport_animfenster.cpp
        src/ns_ui.h
        src/nsimport_res.h
        src/nsimport.rc
        nsimport.def)
    set_target_properties(NSImport PROPERTIES
        SUFFIX ".dlu" PREFIX "" OUTPUT_NAME "NSImport"
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
    target_include_directories(NSImport PRIVATE src)
    target_include_directories(NSImport SYSTEM PRIVATE "${3DSMAX_SDK_DIR}/include")
    target_link_directories(NSImport PRIVATE "${3DSMAX_SDK_DIR}/lib/x64/Release")
    target_link_libraries(NSImport PRIVATE nsleser
        core.lib maxutil.lib bmm.lib Geom.lib mesh.lib
        Maxscrpt.lib MNMath.lib Paramblk2.lib comctl32.lib ole32.lib uuid.lib shell32.lib dwmapi.lib uxtheme.lib advapi32.lib)
    target_compile_definitions(NSImport PRIVATE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
        MAX_JAHRGANG=${MAX_VERSION})
    if(MSVC)
        # /Zp8 ist beim Max-SDK Pflicht (Struct-Packing der SDK-Strukturen).
        target_compile_options(NSImport PRIVATE /EHsc /Zp8 /bigobj /utf-8 /Zc:__cplusplus /external:W0 /W4 /wd4100 /wd4458)
        target_link_options(NSImport PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endif()

message(STATUS "NSImport: Max ${MAX_VERSION}  Plugin=${NS_BUILD_PLUGIN}  Werkzeug=${NS_BUILD_TOOL}")
