# CMakeList.txt: imgui_study 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

# GLFW and Dear ImGui are kept as source in the repository so the project can
# be built without an additional package manager dependency.
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

# 将源代码添加到此项目的可执行文件。
add_executable(glSceneEditor WIN32
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
target_link_libraries(glSceneEditor PRIVATE glfw OpenGL::GL imm32 assimp_prebuilt)

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

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET glSceneEditor PROPERTY CXX_STANDARD 20)
endif()

# TODO: 如有需要，请添加测试并安装目标。
