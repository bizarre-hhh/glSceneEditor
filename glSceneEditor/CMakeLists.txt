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

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# GLFW and Dear ImGui remain the renderer inside the Qt central widget.
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/glfw")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/imgui")
set(APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
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
  "main.cpp"
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
