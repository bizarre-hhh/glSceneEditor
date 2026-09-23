# glSceneEditor

一个基于 C++20 和 OpenGL 3.3 的 3D 场景编辑器示例。项目使用 GLFW 创建窗口，使用 Dear ImGui 提供交互面板，并通过 Assimp 加载示例背包模型。

## 功能

- 加载并渲染 Wavefront OBJ 背包模型
- 通过面板调整模型缩放、线框模式、背景颜色和相机参数
- 使用 WASD 移动相机，按住鼠标右键旋转视角，滚动鼠标滚轮调整视野

## 环境要求

- Windows
- Visual Studio C++ 工具链
- CMake 和 Ninja
- 支持 OpenGL 3.3 的显卡驱动

## 构建

在 Visual Studio Developer PowerShell（确保 `cl`、`cmake` 和 `ninja` 可用）中运行：

```powershell
git clone --recurse-submodules https://github.com/bizarre-hhh/glSceneEditor.git
cd glSceneEditor
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

也可以将 `x64-debug` 替换为 `x64-release`、`x86-debug` 或 `x86-release`。

## 依赖

GLFW 和 Dear ImGui 作为 Git 子模块管理；GLAD、GLM、Assimp 和 stb_image 位于 `3rd_party/` 目录中。
