# glSceneEditor

一个基于 C++20、Qt Widgets 和 OpenGL 3.3 的 3D 场景编辑器示例。Qt 的 `QMainWindow` 提供主窗口、菜单和状态栏；GLFW/OpenGL/Dear ImGui 场景通过 `QWidget::createWindowContainer()` 嵌入中心区域。程序启动时只显示基板，可从 Qt 文件菜单加载 STL 模型。

## 功能

- 通过“文件 → 打开 STL 模型”或 Ctrl+O 加载 STL 文件；加载失败时保留当前场景模型
- 显示 100 mm × 100 mm 的深灰色水平基板，带 10 mm 网格和边缘 5 mm 刻度；1 个场景单位对应 10 mm
- 基板一角为坐标原点 (0, 0, 0)：X 红色和 Y 绿色沿基板两条边，Z 蓝色垂直向上；三根立体坐标轴均长 120 mm，末端有圆锥箭头和清晰的字母标记
- 基板为零厚度平面：从上方看不透明，从下方看为 50% 半透明，并可看到上方模型
- 启动时窗口最大化，OpenGL 背景默认为 RGB(255, 255, 255) 白色
- STL 坐标按毫米和 Z 向上解释，模型在基板的 X/Y 平面居中，最低 Z 点落在基板上
- 通过“视图 → 场景设置”打开面板，调整线框模式、背景颜色和相机参数
- 右键拖动围绕模型旋转，中键拖动平移，滚轮调整相机距离；F 适配场景，Home 恢复启动视角（X 轴在屏幕上水平）

## 代码结构

- `main.cpp`：创建 Qt 应用和主窗口，运行事件循环
- `scene_editor_window.*`：Qt 菜单、STL 文件对话框、状态栏和渲染定时器
- `scene_viewport.*`：GLFW/OpenGL 初始化、场景渲染、资源生命周期和 STL 模型切换
- `camera_controller.*`：键盘、鼠标和滚轮驱动的相机控制
- `base_plate.h`、`stl_model.h`：基板几何和 STL 网格

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
