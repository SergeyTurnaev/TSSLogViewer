# SPDX-License-Identifier: MIT
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)
add_library(tss_update_client STATIC updatecontroller.h ../client/updatedialog.ui)
set_target_properties(tss_update_client PROPERTIES AUTOMOC ON AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../client")
target_include_directories(tss_update_client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(tss_update_client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tss_update_client PUBLIC Qt6::Widgets Qt6::Network Qt6::Concurrent)
if(APPLE)
    enable_language(OBJCXX)
    set_target_properties(tss_update_client PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    set(TSS_SPARKLE_ROOT "${CMAKE_BINARY_DIR}/sparkle" CACHE PATH "Каталог Sparkle 2.10.0")
    if(NOT EXISTS "${TSS_SPARKLE_ROOT}/Sparkle.framework")
        set(archive "${CMAKE_BINARY_DIR}/Sparkle-2.10.0.tar.xz")
        file(DOWNLOAD
            "https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz"
            "${archive}" TLS_VERIFY ON
            EXPECTED_HASH SHA256=c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c)
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${TSS_SPARKLE_ROOT}")
    endif()
    add_library(tss_sparkle SHARED IMPORTED GLOBAL)
    set_target_properties(tss_sparkle PROPERTIES
        IMPORTED_LOCATION "${TSS_SPARKLE_ROOT}/Sparkle.framework/Sparkle"
        FRAMEWORK TRUE
        INTERFACE_INCLUDE_DIRECTORIES "${TSS_SPARKLE_ROOT}/Sparkle.framework/Headers")
    target_sources(tss_update_client PRIVATE macos.mm)
    set_source_files_properties(macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(tss_update_client PUBLIC tss_sparkle "-framework Cocoa")
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../keys/release-public.pem" TSS_UPDATE_RSA_PEM)
    target_sources(tss_update_client PRIVATE linux.cpp linuxmanifest.cpp linuxmanifest.h linuxinstall.cpp linuxinstall.h)
    target_link_libraries(tss_update_client PRIVATE OpenSSL::Crypto)
endif()
configure_file(buildconfig.h.in "${CMAKE_CURRENT_BINARY_DIR}/buildconfig.h" @ONLY)
