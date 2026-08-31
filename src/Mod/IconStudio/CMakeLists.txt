# SPDX-License-Identifier: LGPL-2.1-or-later

SET(IconStudioMain_SRCS
    Init.py
    InitGui.py
    IconStudio.py
    Fitting.py
)
SOURCE_GROUP("" FILES ${IconStudioMain_SRCS})

SET(IconStudioResources
    Resources/guides.svg
)
SOURCE_GROUP("Resources" FILES ${IconStudioResources})

SET(IconStudioTests_SRCS
    IconStudioTests/__init__.py
    IconStudioTests/TestFitting.py
)
SOURCE_GROUP("IconStudioTests" FILES ${IconStudioTests_SRCS})

SET(all_files ${IconStudioMain_SRCS} ${IconStudioResources} ${IconStudioTests_SRCS})

ADD_CUSTOM_TARGET(IconStudio ALL
    SOURCES ${all_files}
)

fc_copy_sources(IconStudio "${CMAKE_BINARY_DIR}/Mod/IconStudio" ${all_files})

INSTALL(
    FILES ${IconStudioMain_SRCS}
    DESTINATION Mod/IconStudio
)
INSTALL(
    FILES ${IconStudioResources}
    DESTINATION Mod/IconStudio/Resources
)
INSTALL(
    FILES ${IconStudioTests_SRCS}
    DESTINATION Mod/IconStudio/IconStudioTests
)
