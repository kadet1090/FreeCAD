target_compile_definitions(Mesh_tests_run PRIVATE DATADIR="${CMAKE_SOURCE_DIR}/data")

target_sources(Mesh_tests_run PRIVATE
    Core/KDTree.cpp
    Elements.cpp
    Exporter.cpp
    Importer.cpp
    Mesh.cpp
    MeshFeature.cpp
)
