cmake_minimum_required(VERSION 3.16)
project(glSceneEditor LANGUAGES C CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use this machine's installed Qt kit when CMake has not received a Qt prefix
# from Qt Creator, Visual Studio, or the command line.
if (NOT CMAKE_PREFIX_PATH AND EXISTS "D:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5")
  list(PREPEND CMAKE_PREFIX_PATH "D:/Qt/Qt5.14.2/5.14.2/msvc2017_64")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Svg)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

# GLFW and Dear ImGui remain the renderer inside the Qt central widget.
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/glfw")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/imgui")
set(APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(IMGUIZMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/ImGuizmo")
set(GLAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/glad")
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/glm")
set(ASSIMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/assimp")
set(STB_IMAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/stb_image")

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${GLFW_DIR}" "${CMAKE_BINARY_DIR}/_deps/glfw-build" EXCLUDE_FROM_ALL)

find_package(OpenGL REQUIRED)

if (WIN32)
  add_library(assimp_prebuilt SHARED IMPORTED GLOBAL)
  set_target_properties(assimp_prebuilt PROPERTIES
    IMPORTED_IMPLIB "${ASSIMP_DIR}/lib/assimp.lib"
    IMPORTED_LOCATION "${ASSIMP_DIR}/bin/assimp-vc140-mt.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_DIR}/include"
  )
else()
  message(FATAL_ERROR "This example currently uses the copied Windows Assimp binary.")
endif()

set(PROJECT_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/../asset/icon/toolbar_icons.qrc"
  "${IMGUIZMO_DIR}/ImGuizmo.cpp"
  "main.cpp"
  "project_file.cpp"
  "recent_projects.cpp"
  "welcome_dialog.cpp"
  "scene_editor_window.cpp"
  "scene_viewport.cpp"
  "camera_controller.cpp"
  "view_cube.cpp"
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_demo.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
  "${GLAD_DIR}/src/glad.c"
  "${STB_IMAGE_DIR}/src/stb_image.cpp"
)

qt_add_executable(glSceneEditor ${PROJECT_SOURCES})

target_include_directories(glSceneEditor PRIVATE
  "${APP_SOURCE_DIR}"
  "${IMGUIZMO_DIR}"
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends"
  "${GLAD_DIR}/include"
  "${GLM_DIR}/include"
  "${ASSIMP_DIR}/include"
  "${STB_IMAGE_DIR}/include"
)

target_compile_definitions(glSceneEditor PRIVATE _CRT_SECURE_NO_WARNINGS)
if (MSVC)
  target_compile_options(glSceneEditor PRIVATE /utf-8)
endif()
target_link_libraries(glSceneEditor PRIVATE
  glfw
  OpenGL::GL
  imm32
  assimp_prebuilt
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Svg
)

if (WIN32)
  set_target_properties(glSceneEditor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set(MODEL_RESOURCE_OUTPUT_DIR "$<TARGET_FILE_DIR:glSceneEditor>/resources/asset/backpack")
set(SHADER_RESOURCE_OUTPUT_DIR "$<TARGET_FILE_DIR:glSceneEditor>/resources/shaders")
add_custom_command(TARGET glSceneEditor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/backpack"
    "${MODEL_RESOURCE_OUTPUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_RESOURCE_OUTPUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/shaders/model.vs"
    "${SHADER_RESOURCE_OUTPUT_DIR}/model.vs"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/shaders/model.fs"
    "${SHADER_RESOURCE_OUTPUT_DIR}/model.fs"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/shaders/base_plate.vs"
    "${SHADER_RESOURCE_OUTPUT_DIR}/base_plate.vs"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/shaders/base_plate.fs"
    "${SHADER_RESOURCE_OUTPUT_DIR}/base_plate.fs"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/shaders/stl.vs"
    "${SHADER_RESOURCE_OUTPUT_DIR}/stl.vs"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../asset/shaders/stl.fs"
    "${SHADER_RESOURCE_OUTPUT_DIR}/stl.fs"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ASSIMP_DIR}/bin/assimp-vc140-mt.dll"
    "$<TARGET_FILE_DIR:glSceneEditor>/assimp-vc140-mt.dll"
)
if (WIN32)
  get_target_property(_qt_core_release Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
  if (NOT _qt_core_release)
    get_target_property(_qt_core_release Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
  endif()
  if (_qt_core_release)
    get_filename_component(_qt_bin_dir "${_qt_core_release}" DIRECTORY)
  endif()
  find_program(QT_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    HINTS "${_qt_bin_dir}" ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES bin
  )
  if (QT_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET glSceneEditor POST_BUILD
      COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:glSceneEditor>"
      COMMENT "Deploying Qt runtime files"
    )
  endif()
endif()

if (BUILD_TESTING)
  set(PROJECT_TEST_SOURCES ${PROJECT_SOURCES})
  list(REMOVE_ITEM PROJECT_TEST_SOURCES "main.cpp")
  add_executable(project_tests "tests/project_tests.cpp" ${PROJECT_TEST_SOURCES})
  get_target_property(_app_include_dirs glSceneEditor INCLUDE_DIRECTORIES)
  get_target_property(_app_link_libraries glSceneEditor LINK_LIBRARIES)
  target_include_directories(project_tests PRIVATE ${_app_include_dirs})
  target_link_libraries(project_tests PRIVATE ${_app_link_libraries})
  target_compile_definitions(project_tests PRIVATE _CRT_SECURE_NO_WARNINGS
    TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../asset")
  if (MSVC)
    target_compile_options(project_tests PRIVATE /utf-8)
  endif()
  set_target_properties(project_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  add_dependencies(project_tests glSceneEditor)
  add_test(NAME project_roundtrip COMMAND project_tests)
  add_test(NAME project_ui_workflow COMMAND project_tests --ui)
  set_tests_properties(project_ui_workflow PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${_qt_bin_dir}/../plugins")
  set_tests_properties(project_roundtrip PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${_qt_bin_dir}/../plugins")
endif()
