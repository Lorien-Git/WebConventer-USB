cmake_minimum_required(VERSION 3.20)
project(webconventer_usb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure for MSVC/Clang on Windows
if(MSVC)
    add_compile_options(/EHsc /W4)
    # Statically link runtime
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# We have two configurations: Debug and Release.
# Output executables to "builds" directory at project root
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/builds")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/builds")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/builds")

# Keep PDB debug symbols inside the temp directory so builds/ stays clean
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temp")
set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/temp")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/temp")

file(GLOB EXT2FS_SRCS external/ext2fs/*.c)

add_executable(webconventer_usb WIN32
    src/main.cpp
    src/DiskManager.cpp
    src/Resource.cpp
    src/ext2fs_format.c
    ${EXT2FS_SRCS}
)

# Set executable name based on config
set_target_properties(webconventer_usb PROPERTIES 
    OUTPUT_NAME_DEBUG "webconventer-usb-debug"
    OUTPUT_NAME_RELEASE "webconventer-usb"
)

# For Debug we don't want WIN32 subsystem (we want console)
# So we need to tweak the linker flag based on configuration
set_target_properties(webconventer_usb PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

target_include_directories(webconventer_usb PRIVATE external src external/ext2fs)
target_compile_definitions(webconventer_usb PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(webconventer_usb PRIVATE ws2_32 crypt32 shell32 user32 Shlwapi WbemUuid version comdlg32 ntdll uuid setupapi advapi32)

# Request Administrator privileges
set_target_properties(webconventer_usb PROPERTIES LINK_FLAGS "-Xlinker /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

# Use Miniz for compression
target_sources(webconventer_usb PRIVATE external/miniz.c)

# Add custom command to pack frontend files into Resource.cpp
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/Resource.cpp
    COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/src/pack_resources.ps1
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/frontend/index.html
            ${CMAKE_CURRENT_SOURCE_DIR}/frontend/style.css
            ${CMAKE_CURRENT_SOURCE_DIR}/frontend/script.js
            ${CMAKE_CURRENT_SOURCE_DIR}/src/pack_resources.ps1
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Packing frontend resources into C++ array..."
)
