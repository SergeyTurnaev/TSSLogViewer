cmake_minimum_required(VERSION 3.16)
project(TSSLogViewer VERSION 26.2 LANGUAGES CXX)

file(WRITE "${CMAKE_BINARY_DIR}/VERSION.txt" "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport OpenGLWidgets LinguistTools)
find_package(OpenGL REQUIRED)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SOURCES
    ${SRC_DIR}/controlpanel.cpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/mainwindow.cpp
    ${SRC_DIR}/openlogwidget.cpp
    ${SRC_DIR}/plotview.cpp
    ${SRC_DIR}/progressmessagebox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qcustomplot/qcustomplot.cpp
    ${SRC_DIR}/logdescriptionwidget.cpp
)

set(HEADERS
    ${SRC_DIR}/controlpanel.h
    ${SRC_DIR}/mainwindow.h
    ${SRC_DIR}/openlogwidget.h
    ${SRC_DIR}/plotview.h
    ${SRC_DIR}/progressmessagebox.h
    ${SRC_DIR}/statistic.h
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qcustomplot/qcustomplot.h
    ${SRC_DIR}/logdescriptionwidget.h
)

set(FORMS
    ${SRC_DIR}/controlpanel.ui
    ${SRC_DIR}/mainwindow.ui
    ${SRC_DIR}/openlogwidget.ui
    ${SRC_DIR}/plotview.ui
    ${SRC_DIR}/logdescriptionwidget.ui
)

set(TS_FILES
    resources/ts/app_en.ts
    resources/ts/app_ru.ts
    resources/ts/app_de.ts
    resources/ts/app_zh_TW.ts
    resources/ts/app_it.ts
    resources/ts/app_fr.ts
    resources/ts/app_es.ts
    resources/ts/app_ro.ts
    resources/ts/app_pl.ts
    resources/ts/app_ja.ts
    resources/ts/app_cs.ts
)

set(APP_ICON_MACOSX ${CMAKE_CURRENT_SOURCE_DIR}/icon/TSSLogViewer.icns)

qt_add_executable(TSSLogViewer WIN32 MACOSX_BUNDLE
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
    resources/common.qrc
    resources/icon.qrc
    $<$<BOOL:${APPLE}>:${APP_ICON_MACOSX}>
)

if(APPLE)
    set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_target_properties(TSSLogViewer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/TSSLogViewer.Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "TSSLogViewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "TSSLogViewer.icns"
    )
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS TSSLogViewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/TSSLogViewer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(SIZE 64 128 256 512 1024)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icon/${SIZE}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${SIZE}x${SIZE}/apps
            RENAME TSSLogViewer.png)
    endforeach()
endif()

qt_add_translations(TSSLogViewer
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/ts"
)

configure_file(${SRC_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

target_include_directories(TSSLogViewer PRIVATE
    ${SRC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qcustomplot
)

target_compile_definitions(TSSLogViewer PRIVATE
    QCUSTOMPLOT_USE_OPENGL
    SUPPORTED_LANGUAGES="en ru de zh_TW it fr es ro pl ja cs"
)

target_link_libraries(TSSLogViewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::OpenGLWidgets
    OpenGL::GL
)

if(WIN32 AND EXISTS ${SRC_DIR}/tsslogviewver.rc)
    target_sources(TSSLogViewer PRIVATE ${SRC_DIR}/tsslogviewver.rc)
endif()
