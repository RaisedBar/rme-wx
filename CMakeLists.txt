# Cross-platform build for TotalMix Access.
#
#   Windows: cmake --preset windows-vs2022     -> build/windows-vs2022/TotalMixAccess.sln
#            (the hand-maintained TotalMixAccess.sln in the repository root also works)
#   macOS:   cmake --preset macos-xcode        -> build/macos-xcode/TotalMixAccess.xcodeproj
#
# wxWidgets comes from vcpkg in classic mode (vcpkg install wxwidgets); the presets use
# $env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake as the toolchain file.

cmake_minimum_required(VERSION 3.21)

project(TotalMixAccess VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
# Portable Windows release: with a static vcpkg triplet (x64-windows-static) link the C runtime
# statically too, so TotalMixAccess.exe runs on its own with no DLLs.
if(MSVC AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

find_package(wxWidgets CONFIG REQUIRED)

if(MSVC)
    set(TM_WARNINGS /W4 /utf-8 /permissive- /Zc:__cplusplus)
else()
    set(TM_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Core library (no GUI) -------------------------------------------------------------
add_library(TotalMixCore STATIC
    src/core/Osc.cpp       src/core/Osc.h
    src/core/UdpSocket.cpp src/core/UdpSocket.h
    src/core/Scale.cpp     src/core/Scale.h
    src/core/Params.cpp    src/core/Params.h
    src/core/Link.cpp      src/core/Link.h
    src/core/PortCheck.cpp src/core/PortCheck.h
)
target_include_directories(TotalMixCore PUBLIC src/core)
target_compile_options(TotalMixCore PRIVATE ${TM_WARNINGS})
if(WIN32)
    target_compile_definitions(TotalMixCore PUBLIC NOMINMAX UNICODE _UNICODE)
    target_link_libraries(TotalMixCore PUBLIC ws2_32 iphlpapi)
endif()

# ---- Tests ----------------------------------------------------------------------------
enable_testing()
add_executable(TotalMixTests tests/Tests.cpp)
target_link_libraries(TotalMixTests PRIVATE TotalMixCore)
target_compile_options(TotalMixTests PRIVATE ${TM_WARNINGS})
add_test(NAME core COMMAND TotalMixTests)

# ---- GUI application ------------------------------------------------------------------
set(APP_SOURCES
    src/app/App.cpp               src/app/App.h
    src/app/AppContext.h
    src/app/MainFrame.cpp         src/app/MainFrame.h
    src/app/MixerPanel.cpp        src/app/MixerPanel.h
    src/app/PagePanel.cpp         src/app/PagePanel.h
    src/app/BusSelector.cpp       src/app/BusSelector.h
    src/app/ParamControls.cpp     src/app/ParamControls.h
    src/app/OscSettingsDialog.cpp src/app/OscSettingsDialog.h
    src/app/SettingsStore.cpp     src/app/SettingsStore.h
    src/app/ChangeAnnouncer.cpp   src/app/ChangeAnnouncer.h
    src/app/Accessibility.h
)
if(WIN32)
    list(APPEND APP_SOURCES src/app/Accessibility_msw.cpp src/app/Announce_msw.cpp src/app/TotalMixAccess.rc)
elseif(APPLE)
    list(APPEND APP_SOURCES src/app/Accessibility_mac.mm)
else()
    list(APPEND APP_SOURCES src/app/Accessibility_generic.cpp)
endif()

add_executable(TotalMixAccess WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(TotalMixAccess PRIVATE TotalMixCore wx::core wx::base wx::wxxml)
target_compile_options(TotalMixAccess PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${TM_WARNINGS}>)

if(WIN32)
    target_link_libraries(TotalMixAccess PRIVATE oleacc uiautomationcore)
    # The application manifest (DPI awareness, common controls v6) comes from wx/msw/wx.rc.
    target_link_options(TotalMixAccess PRIVATE /MANIFEST:NO)
elseif(APPLE)
    target_link_libraries(TotalMixAccess PRIVATE "-framework AppKit")
    set_target_properties(TotalMixAccess PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.totalmix-access"
        MACOSX_BUNDLE_BUNDLE_NAME "TotalMix Access"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.example.totalmix-access"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TotalMixAccess)
