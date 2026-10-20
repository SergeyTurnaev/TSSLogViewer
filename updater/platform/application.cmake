# SPDX-License-Identifier: MIT
# Подключается из обоих приложений до qt_add_translations().
set(TSS_UPDATE_APP "${PROJECT_NAME}")
set(TSS_UPDATE_VERSION "${PROJECT_VERSION}")
find_package(Qt6 REQUIRED COMPONENTS Network Concurrent)
if(NOT DEFINED TSS_UPDATE_BUILD)
    string(TIMESTAMP TSS_UPDATE_BUILD "%s" UTC)
endif()
if(NOT TSS_UPDATE_BUILD MATCHES "^[1-9][0-9]+$")
    message(FATAL_ERROR "TSS_UPDATE_BUILD должен быть положительным номером сборки")
endif()
set(TSS_UPDATE_BUILD "${TSS_UPDATE_BUILD}" CACHE STRING "Монотонный номер сборки обновления")
if(NOT DEFINED TSS_UPDATE_CHANNEL)
    if(PROJECT_NAME STREQUAL "TSSLimiterX")
        set(TSS_UPDATE_CHANNEL limiter)
    elseif(APPLE)
        # Публичный CI без Developer ID имеет отдельную ленту.
        set(TSS_UPDATE_CHANNEL viewer-community)
    else()
        set(TSS_UPDATE_CHANNEL viewer)
    endif()
endif()
set(TSS_UPDATE_CHANNEL "${TSS_UPDATE_CHANNEL}" CACHE STRING "Канал обновлений")
if(NOT TSS_UPDATE_CHANNEL MATCHES "^(limiter|viewer|viewer-community)$")
    message(FATAL_ERROR "Неизвестный канал обновлений")
endif()
get_filename_component(TSS_UPDATER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(PROJECT_NAME STREQUAL "TSSLimiterX")
    set(TSS_UPDATER_BUILD_HELPER OFF)
endif()
add_subdirectory("${TSS_UPDATER_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/updater")
target_link_libraries(${PROJECT_NAME} PRIVATE tss_update_client)
set(UPDATE_TRANSLATION_SOURCES
    "${TSS_UPDATER_DIR}/client/updatecontroller.cpp"
    "${TSS_UPDATER_DIR}/client/updatedialog.ui"
    "${TSS_UPDATER_DIR}/platform/linux.cpp"
    "${TSS_UPDATER_DIR}/platform/macos.mm")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/UPDATE_BUILD.txt" "${TSS_UPDATE_BUILD}\n")
if(APPLE)
    file(READ "${TSS_UPDATER_DIR}/keys/sparkle-public.txt" TSS_SPARKLE_PUBLIC_KEY)
    string(STRIP "${TSS_SPARKLE_PUBLIC_KEY}" TSS_SPARKLE_PUBLIC_KEY)
    # CFBundleVersion: не более трёх целых частей, каждая короче пяти цифр.
    math(EXPR mac_major "${TSS_UPDATE_BUILD} / 100000000")
    math(EXPR mac_minor "(${TSS_UPDATE_BUILD} / 10000) % 10000")
    math(EXPR mac_patch "${TSS_UPDATE_BUILD} % 10000")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_VERSION "${mac_major}.${mac_minor}.${mac_patch}"
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Frameworks"
        COMMAND /usr/bin/ditto "${TSS_SPARKLE_ROOT}/Sparkle.framework"
            "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Frameworks/Sparkle.framework"
        VERBATIM)
endif()
