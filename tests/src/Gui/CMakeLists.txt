# SPDX-License-Identifier: LGPL-2.1-or-later

add_subdirectory(Dialogs)
add_subdirectory(propertyeditor)

# Standard C++ GTest tests
add_executable(Gui_tests_run
        Assistant.cpp
        Camera.cpp
        ColorScaleOverlayTest.cpp
        FileDialog.cpp
        StyleParameters/StyleParametersApplicationTest.cpp
        StyleParameters/ParserTest.cpp
        StyleParameters/ParserRobustnessTest.cpp
        StyleParameters/ParameterManagerTest.cpp
        StyleParameters/YamlParameterSourceTest.cpp
        StyleParameters/DiagnosticsTest.cpp
        StyleParameters/ValueAccessTest.cpp
        StyleParameters/EdgesTest.cpp
        StyleParameters/CornersTest.cpp
        StyleParameters/GradientTest.cpp
        StyleParameters/ColorShadingTest.cpp
        StyleParameters/ColorEffectTest.cpp
        StyleParameters/StyleContextTest.cpp
        StyleParameters/ShippedThemeTest.cpp
        InputHintTest.cpp
        SelectionTest.cpp
        ThumbnailCamera.cpp
)

# ShippedThemeTest reads the theme YAML out of the build's data directory, so the copy has to
# have happened before the suite runs - otherwise it silently checks a stale snapshot.
add_dependencies(Gui_tests_run Stylesheets_data)

# Qt tests
setup_qt_test(ObjectSelection)
setup_qt_test(QuantitySpinBox)
setup_qt_test(PushButtonStyle)

target_link_libraries(Gui_tests_run PRIVATE
    GTest::gmock_main
    FreeCADApp
    FreeCADGui
)
