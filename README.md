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
git clone https://github.com/bizarre-hhh/glSceneEditor.git
cd glSceneEditor

cmake --preset x64-debug
cmake --build out/build/x64-debug
```

也可以将 `x64-debug` 替换为 `x64-release`、`x86-debug` 或 `x86-release`。

## 依赖

GLFW、Dear ImGui、GLAD、GLM、Assimp 和 stb_image 的项目所需文件直接保存在 `3rd_party/` 目录中。项目编译 ImGui 核心代码，并使用 GLFW 输入和 OpenGL 3 后端；模型导入由 Assimp 提供，纹理解码由 stb_image 提供。
