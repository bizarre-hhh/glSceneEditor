# glSceneEditor

一个基于 C++20、Qt Widgets 和 OpenGL 3.3 的 3D 场景编辑器示例。Qt 的 `QMainWindow` 提供主窗口、菜单和状态栏；现有 GLFW/OpenGL/Dear ImGui 场景通过 `QWidget::createWindowContainer()` 嵌入中心区域，并通过 Assimp 加载示例背包模型。

## 功能

- 加载并渲染 Wavefront OBJ 背包模型
- 通过面板调整模型缩放、线框模式、背景颜色和相机参数
- 使用 WASD 移动相机，按住鼠标右键旋转视角，滚动鼠标滚轮调整视野

## 环境要求

- Windows
- Visual Studio C++ 工具链
- CMake 和 Ninja
- Qt Widgets 开发套件（当前预设默认使用 `D:/Qt/Qt5.14.2/5.14.2/msvc2017_64`；安装在其他位置时，修改 `CMakePresets.json` 中的 `CMAKE_PREFIX_PATH`）
- 支持 OpenGL 3.3 的显卡驱动

## 构建

在 Visual Studio Developer PowerShell（确保 `cl`、`cmake` 和 `ninja` 可用）中运行：

```powershell
git clone --recurse-submodules https://github.com/bizarre-hhh/glSceneEditor.git
cd glSceneEditor

# 可选：只在工作目录中检出项目使用的 GLFW/ImGui 文件
git -C 3rd_party/glfw sparse-checkout set --cone CMake deps include src
$imguiFiles = @(
  '/.editorconfig',
  '/.gitattributes',
  '/.gitignore',
  '/imconfig.h',
  '/imgui_demo.cpp',
  '/imgui_draw.cpp',
  '/imgui_internal.h',
  '/imgui_tables.cpp',
  '/imgui_widgets.cpp',
  '/imgui.cpp',
  '/imgui.h',
  '/imstb_rectpack.h',
  '/imstb_textedit.h',
  '/imstb_truetype.h',
  '/LICENSE.txt',
  '/backends/imgui_impl_glfw.cpp',
  '/backends/imgui_impl_glfw.h',
  '/backends/imgui_impl_opengl3.cpp',
  '/backends/imgui_impl_opengl3.h',
  '/backends/imgui_impl_opengl3_loader.h'
)
$imguiFiles | git -C 3rd_party/imgui sparse-checkout set --no-cone --stdin

cmake --preset x64-debug
cmake --build out/build/x64-debug
```

也可以将 `x64-debug` 替换为 `x64-release`、`x86-debug` 或 `x86-release`。

上面的稀疏检出会省去 GLFW 的文档、示例和测试，以及 ImGui 的文档、示例、辅助工具和未使用后端；保留构建所需源码、头文件、许可证和本项目使用的 ImGui 后端。设置只保存在本地子模块配置中，不改变上游子模块版本。若要恢复完整文件，运行：

```powershell
git -C 3rd_party/glfw sparse-checkout disable
git -C 3rd_party/imgui sparse-checkout disable
```

## 依赖

GLFW 和 Dear ImGui 作为 Git 子模块管理；GLAD、GLM、Assimp 和 stb_image 位于 `3rd_party/` 目录中。当前项目编译 ImGui 核心代码，并使用 GLFW 输入和 OpenGL 3 后端；模型导入由 Assimp 提供，纹理解码由 stb_image 提供。
