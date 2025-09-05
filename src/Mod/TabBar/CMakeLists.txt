set(TabBar_SRCS
    README.md
    InitGui.py
    TabBar_Gui.py
)
source_group("" FILES ${TabBar_SRCS})

set(TabBar_Resources
    Resources/icons/TabBar_AddWorkbench.svg
    Resources/icons/TabBar_MoveDown.svg
    Resources/icons/TabBar_MoveUp.svg
)

set(TabBar_Licence
    License/lgpl-2.1.txt
)

set(all_files ${TabBar_SRCS} ${TabBar_Resources} ${TabBar_Licence})

add_custom_target(TabBar ALL
    SOURCES ${all_files}
)

fc_copy_sources(TabBar "${CMAKE_BINARY_DIR}/Mod/TabBar" ${all_files})

install(
    FILES
        ${TabBar_SRCS}
    DESTINATION
        Mod/TabBar
)

install(
    FILES
        ${TabBar_Resources}
    DESTINATION
        Mod/TabBar/Resources/icons
)

install(
    FILES
        ${TabBar_Licence}
    DESTINATION
        Mod/TabBar/License
)
