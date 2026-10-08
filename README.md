# glSceneEditor

基于 C++20、Qt Widgets 和 OpenGL 3.3 的 STL 场景编辑器。Qt 主窗口提供菜单、工具栏、模型列表和信息面板，中心区域嵌入 GLFW/OpenGL 视口，场景设置使用 Dear ImGui 绘制。

程序启动时最大化窗口，显示白色背景、基板和 XYZ 坐标轴。支持同时加载多个 STL 模型，进行选择、显示或隐藏、包围盒查看和 XY 平移。

## 构建与运行

### 环境要求

- Windows，支持 C++20 的 Visual Studio / MSVC C++ 工具链。
- CMake 3.21 或更新版本（使用版本 3 的 CMake Presets），以及 Ninja。
- 与编译器及目标架构匹配的 Qt 5 或 Qt 6 Widgets 开发套件。
- 支持 OpenGL 3.3 的显卡驱动。

当前预设使用 x64 Qt 套件 `D:/Qt/Qt5.14.2/5.14.2/msvc2017_64`，仓库中的 Assimp 预编译库也为 x64，建议使用 `x64-debug` 或 `x64-release`。

### Debug 构建

在已初始化 **x64 MSVC 编译环境** 的 Visual Studio Developer PowerShell 中运行，确保 `cl`、`cmake` 和 `ninja` 可用：

```powershell
git clone https://github.com/bizarre-hhh/glSceneEditor.git
cd glSceneEditor

cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\glSceneEditor\glSceneEditor.exe
```

如果 Qt 安装在其他位置，可修改 `CMakePresets.json` 中的 `CMAKE_PREFIX_PATH`，或在配置时覆盖该值。下面命令中的路径需要替换成实际 Qt 套件目录：

```powershell
cmake --preset x64-debug -DCMAKE_PREFIX_PATH="C:/Qt/你的Qt版本/你的MSVC套件"
```

### Release 构建

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release
.\out\build\x64-release\glSceneEditor\glSceneEditor.exe
```

预设中还保留了 `x86-debug` 和 `x86-release`。使用它们前，需要配置 x86 编译环境，并提供匹配的 32 位 Qt 套件、Assimp 导入库和 DLL；当前仓库的 x64 依赖不能直接用于 x86 构建。

构建后会将着色器复制到可执行文件旁的 `resources/shaders/`，并复制 Assimp DLL。找到 `windeployqt` 时，还会自动部署 Qt 运行库。运行或复制程序时，需要保留这些资源和运行库，不能只复制 `.exe` 文件。

## 加载和管理模型

通过工具栏的“打开 STL”、菜单“文件 → 打开 STL 模型”或 `Ctrl+O` 打开文件对话框，可一次选择多个文件，也可重复导入同一文件。新模型追加到已有场景；部分文件加载失败时，已加载的模型和其他成功导入的模型会保留。

文件对话框当前默认打开 `D:\vs_cmake_proj\glSceneEditor\asset`。在其他目录使用项目时，可修改 [scene_editor_window.cpp](glSceneEditor/scene_editor_window.cpp) 中 `OpenStl()` 的 `initial_directory`。

STL 坐标按毫米、Z 轴向上解释。每个模型加载后在基板的 X/Y 方向居中，最低 Z 点落在基板上。多个模型可能重叠，可选中后拖动调整位置。添加、隐藏和删除模型不会自动改变相机视角；需要重新取景时使用 Home 或“重置视角”。

左侧“STL 模型”窗口列出所有已加载模型。**点击列表行或模型名称不会选择模型**，列表中通过对应按钮操作：

| 按钮 | 功能 |
| --- | --- |
| 选中 / 取消选中 | 切换该模型的选择状态。支持同时选中多个模型，与视口中的选择同步。 |
| 隐藏 / 显示 | 切换模型可见性。隐藏后仍保留模型和选择状态，隐藏模型不能在视口中被拾取。 |
| 显示包围盒 / 隐藏包围盒 | 切换世界坐标包围盒的蓝色线框，默认关闭。被模型或基板遮挡的线段不显示，拖动模型时同步更新。隐藏模型时包围盒也隐藏，再显示模型时保留原开关状态。 |
| 删除 | 从场景和列表中移除模型，并释放图形资源，不删除原始 STL 文件。 |

模型窗口可调整宽度、关闭或移动到右侧；拖动列表与信息区之间的分隔条可调整高度。“视图”菜单可重新显示模型窗口和工具栏。

### 模型信息

只有模型被选中时才显示信息。多选时显示最近选中的模型；取消该模型的选择或删除它后，显示其余已选模型中的最近一项。没有选中模型时，信息区清空。

| 区域 | 内容 |
| --- | --- |
| 基本信息 | 文件名、完整路径、文件大小、三角面数和网格的 X/Y/Z 尺寸（mm）。 |
| 包围盒位置 | 世界坐标下的包围盒中心、最小位置和最大位置，分别显示 X/Y/Z 数值（mm）。 |
| 自身变换 | 相对加载后初始姿态的 X/Y/Z 位移（mm）、缩放（倍）和旋转（°）。初始位移和旋转为 0，缩放为 1。 |

信息只读且可复制。当前交互编辑支持 X/Y 平移，拖动时实时更新包围盒位置和自身位移；信息区暂不提供 Z 位移、缩放或旋转的编辑控件。

## 视口操作

以下鼠标和键盘操作在 OpenGL 视口中使用：

| 操作 | 效果 |
| --- | --- |
| 左键点击模型 | 选中模型，橙色高亮，并突出显示对应列表行。不会取消其他模型的选择；重叠处拾取离相机最近的可见模型。 |
| 在已选中模型上按住左键拖动 | 移动被拖动的模型，其他模型保持原位。只改变世界 X/Y，Z 值固定；可拖出基板范围。第一次点击未选中的模型只负责选中。 |
| 左键点击空白区域 | 取消所有模型的选择并清空信息区。 |
| 右键拖动 | 围绕当前观察中心旋转相机，视角立方体同步旋转。 |
| 中键拖动 | 平移视图。 |
| 鼠标滚轮 | 调整观察距离。 |
| Home 键 | 恢复默认透视视角，并按当前场景范围重新取景。 |

松开左键结束模型拖动；隐藏、删除或取消选中正在拖动的模型也会结束拖动。移动模型不会修改原始 STL 文件。

### 视角立方体与 Home

视口左下角的立方体实时显示相机方向。左键点击 Front、Back、Left、Right、Top、Bottom 面，切换到前、后、左、右、上、下标准视图，保留当前观察中心、距离和模型选择。六个标准视图均使用正交投影：

- 前、后视图显示 X 和 Z 轴。
- 左、右视图显示 Y 和 Z 轴。
- 上、下视图显示 X 和 Y 轴。

沿视线方向的坐标轴及其标记会隐藏。立方体的面有悬浮高亮，不显示提示气泡；在立方体上按住右键也可拖动旋转视角，拖出立方体后仍可继续，松开右键结束。

立方体下方的 Home 按钮、工具栏及菜单中的“重置视角”和 Home 键使用相同的默认视角。重置时根据基板、坐标轴和所有已加载模型（包括隐藏模型）重新取景，将场景放大并向上调整布局，同时保留模型选择。

### 场景设置与投影

通过工具栏或“视图 → 场景设置”打开设置面板，可调整线框模式、背景颜色和相机参数。

`Projection` 下拉框实时显示当前投影模式，可选择 `Perspective`（透视）或 `Orthographic`（正交）。手动切换投影时，保留相机方向、观察中心、距离和模型状态。

点击立方体的标准视图面会切换为正交投影；中键平移和滚轮缩放保持当前投影模式。右键旋转、编辑相机 Yaw/Pitch 或 Home 重置会恢复透视投影，下拉框同步更新。

## 坐标与基板

- 1 个场景单位对应 10 mm，STL 文件中的数值按毫米解释。
- 基板为 100 mm × 100 mm 的水平平面，网格间距为 10 mm，边缘刻度间距为 5 mm。
- 基板一角为原点 `(0, 0, 0)`，X/Y 轴沿基板两条边，Z 轴垂直向上。
- X、Y、Z 轴分别为红、绿、蓝色，长度均为 120 mm，末端显示箭头和字母标记。
- 基板为零厚度平面，从上方看不透明，从下方看为 50% 半透明。

## 示例模型

[asset](asset/) 中提供以下简单 STL 文件，可用于多模型加载、选择、拖动和包围盒功能的验证：

| 文件 | 模型 |
| --- | --- |
| [cube_30mm.stl](asset/cube_30mm.stl) | 立方体 |
| [sphere_30mm.stl](asset/sphere_30mm.stl) | 球体 |
| [cylinder_30mm.stl](asset/cylinder_30mm.stl) | 圆柱体 |
| [cone_30mm.stl](asset/cone_30mm.stl) | 圆锥体 |
| [torus_30mm.stl](asset/torus_30mm.stl) | 圆环 |
| [pyramid_30mm.stl](asset/pyramid_30mm.stl) | 棱锥 |

## 代码结构

| 文件 / 目录 | 职责 |
| --- | --- |
| [CMakePresets.json](CMakePresets.json) | Windows Ninja 构建预设和 Qt 路径配置。 |
| [glSceneEditor/CMakeLists.txt](glSceneEditor/CMakeLists.txt) | 程序目标、依赖链接、资源复制和运行库部署。 |
| [main.cpp](glSceneEditor/main.cpp) | 创建 Qt 应用和主窗口，运行事件循环。 |
| [scene_editor_window.cpp](glSceneEditor/scene_editor_window.cpp)、[scene_editor_window.h](glSceneEditor/scene_editor_window.h) | Qt 菜单、工具栏、模型列表与信息窗口、文件对话框、状态栏和渲染定时器。 |
| [scene_viewport.cpp](glSceneEditor/scene_viewport.cpp)、[scene_viewport.h](glSceneEditor/scene_viewport.h) | GLFW/OpenGL 初始化、场景渲染、资源管理、多模型管理、拾取和 XY 拖动。 |
| [camera.h](glSceneEditor/camera.h)、[camera_controller.cpp](glSceneEditor/camera_controller.cpp)、[camera_controller.h](glSceneEditor/camera_controller.h) | 相机状态、投影和鼠标、键盘、标准视图控制。 |
| [view_cube.cpp](glSceneEditor/view_cube.cpp)、[view_cube.h](glSceneEditor/view_cube.h) | 视角立方体的绘制、面识别和交互。 |
| [base_plate.h](glSceneEditor/base_plate.h)、[stl_model.h](glSceneEditor/stl_model.h) | 基板与坐标轴几何、STL 网格导入和绘制。 |
| [asset/shaders](asset/shaders/) | 场景使用的着色器。 |
| [3rd_party](3rd_party/) | 仓库内的第三方依赖文件。 |

## 依赖

GLFW、Dear ImGui、GLAD、GLM、Assimp 和 stb_image 的项目所需文件保存在 `3rd_party/` 中。GLFW 管理渲染窗口和输入，Dear ImGui 使用 GLFW / OpenGL 3 后端绘制场景设置，GLAD 加载 OpenGL 函数，GLM 提供数学运算，Assimp 导入模型。Qt 开发套件和 MSVC 工具链需要另行安装。

当前构建使用仓库中的 Windows Assimp 预编译库，因此项目的构建配置面向 Windows。
