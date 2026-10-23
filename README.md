# glSceneEditor

基于 C++20、Qt Widgets 和 OpenGL 3.3 的 Windows STL 场景编辑器。Qt 负责主窗口、菜单、工具栏、模型列表和信息面板，中央区域嵌入 GLFW/OpenGL 视口，场景设置由 Dear ImGui 绘制。

主要功能：

- 多 STL 导入、选择、隐藏、包围盒和重命名
- 平移、旋转、缩放参数窗口与视口 gizmo
- 复制模型、矩形阵列、贴合基板
- 基于 PySLM C++ 的切片和逐层预览
- `.glproj` 单文件工程、撤销/回撤、最近工程记录

## 构建与运行

### 环境要求

- Windows x64，支持 C++20 的 Visual Studio / MSVC 工具链
- CMake 3.21 或更新版本和 Ninja
- Qt 5 或 Qt 6 Widgets 开发套件（与编译器架构匹配）
- 支持 OpenGL 3.3 的显卡驱动

项目使用的 Assimp 和 PySLM C++ 预编译库均为 x64。

### 构建

在已初始化 x64 MSVC 编译环境的 Developer PowerShell 中运行：

```powershell
git clone https://github.com/bizarre-hhh/glSceneEditor.git
cd glSceneEditor

cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\glSceneEditor\glSceneEditor.exe
```

Release 构建：

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release
.\out\build\x64-release\glSceneEditor\glSceneEditor.exe
```

预设默认使用 `D:/Qt/Qt5.14.2/5.14.2/msvc2017_64`。Qt 安装在其他位置时，修改 `CMakePresets.json` 中的 `CMAKE_PREFIX_PATH`，或配置时覆盖：

```powershell
cmake --preset x64-release -DCMAKE_PREFIX_PATH="C:/Qt/你的Qt版本/你的MSVC套件"
```

构建后会把着色器复制到程序旁，并复制 Assimp、PySLM 及其依赖 DLL；找到 `windeployqt` 时还会部署 Qt 运行库，因此不能只复制 `.exe`。预设中的 x86 配置需要匹配的 32 位 Qt 和依赖，当前仓库的 x64 文件不能直接使用。

## 使用说明

### 工程

程序启动时显示欢迎界面，可新建或打开工程，并列出最多 5 个最近打开的工程。工程保存为 `.glproj` 单文件（当前版本 5，兼容读取版本 1～4）。

工程内嵌完整 STL 数据，因此原始 STL 文件移动或删除后仍可正常打开。保存使用临时文件原子替换，写入失败时保留原文件；打开失败时当前场景保持不变。

常用快捷键：

| 操作 | 快捷键 |
| --- | --- |
| 新建 / 打开 / 保存 / 另存为 | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` |
| 导入 STL | `Ctrl+I` |
| 撤销 / 回撤 | `Ctrl+Z` / `Ctrl+Y`、`Ctrl+Shift+Z` |
| 复制 / 矩形阵列 / 贴合基板 | `Ctrl+D` / `Ctrl+Shift+D` / `Ctrl+L` |
| 重置视角 | `Home` |

自上次保存后有新增操作记录时，窗口标题带 `*`。新建、打开其他工程或退出前会提示保存；相机、选择、隐藏等显示状态不会标记工程修改。

默认的工程目录和 STL 目录分别由 `glSceneEditor/project_file.cpp` 中的 `DefaultOpenDirectory()` 和 `scene_editor_window.cpp` 中的 `OpenStl()` 指定。

### 模型管理

通过“文件 → 导入 STL 模型”、工具栏或 `Ctrl+I` 可一次导入一个或多个 STL。STL 坐标按毫米、Z 轴向上解释；模型导入后在平台 X/Y 方向居中，最低点落在 Z = 0。

左侧“STL 模型”窗口提供选择、隐藏/显示、包围盒和删除按钮，每次导入、删除或变换都会进入撤销记录。双击列表行或模型名称可以改名；名称不能为空、不能重复，并随工程保存。

选中模型后，信息面板显示名称、来源路径、文件大小、三角面数、世界坐标包围盒和自身变换。点击“平移 / 缩放 / 旋转”按钮会打开对应参数窗口，同时显示视口 gizmo；两种方式修改的是同一个变换，拖动 gizmo 时参数实时更新。

### 复制、阵列与贴合基板

- **复制模型**（`Ctrl+D`）：为每个选中模型创建一个副本，沿 +X 方向放在原模型组旁边，组间保留 5 mm 间隙。
- **矩形阵列**（`Ctrl+Shift+D`）：设置行数、列数和 X/Y 间隙。行列数包含原模型，多选时把整组作为一个单元重复排列；单次最多新增 1000 个模型。
- **贴合基板**（`Ctrl+L`）：按模型变换后的真实网格最低点沿 Z 移动，使最低 Z 为 0，保持 X/Y 位置、旋转和缩放不变。

以上操作都作为一步撤销，副本拥有独立的模型名称和 STL 数据。

### 切片预览

使用“切片 → 生成切片…”或工具栏的“切片”按钮。需要先导入模型并关闭模型变换参数窗口；所有模型的 X/Y 包围盒必须位于平台范围内。

| 参数 | 默认值 | 范围 |
| --- | --- | --- |
| 层厚 | 0.03 mm | 0.001～10 mm |
| 启用填充 | 开启 | — |
| 扫描间距 | 0.08 mm | 0.001～10 mm |
| 扫描角度 | 45° | 0～180° |
| 层间角度增量 | 67°/层 | -180～180° |

切片按平台坐标处理全部模型（包括隐藏模型），Z = 0 以下的部分不切。层平面位于 `Z = (i + 0.5) × 层厚`，没有轮廓的空层会省略。最多允许 20000 层；预计超过 5000 层或 200 万个三角面时会先提示，切片过程中可以取消。

生成后右侧打开“切片预览”面板：

- 用滑块或层号切换当前层
- 分别显示/隐藏轮廓（青色）、填充（品红）、方向箭头（白色）和跳转（绿色）
- 可隐藏全部模型，只查看当前层切片
- 显示本层的轮廓、填充和跳转统计，以及整个切片的轮廓、填充和总路径长度

预览结果不会写入工程、不会导出文件，也不进入撤销记录。模型、平台尺寸或变换发生变化后，旧预览会自动清除；可点击“重新切片…”按当前设置重算，或使用“清除切片预览”关闭。

### 视口与视角

| 操作 | 效果 |
| --- | --- |
| 左键点击模型 | 选中并高亮模型 |
| 在已选中模型上按住左键拖动 | 沿世界 X/Y 移动模型，Z 不变 |
| 左键点击空白处 | 取消所有选择 |
| 右键拖动 | 环绕观察中心旋转视角 |
| 中键拖动 | 平移视图 |
| 滚轮 | 透视模式下以光标处内容为中心缩放，正交模式下居中缩放 |
| `Home` | 恢复默认透视视角并按场景重新取景 |

左下角的视角立方体显示当前相机方向，点击前、后、左、右、上、下面可切换到对应的正交标准视图；在立方体上按住右键拖动同样可以旋转视角。

“视图 → 场景设置”可调整背景颜色、线框模式和透视/正交投影。

### 坐标与基板

- 1 个场景单位对应 10 mm，STL 中的数值按毫米解释。
- 基板默认 100 mm × 100 mm；Z 高度默认 100 mm，只记录在工程中，不在视口绘制。
- “视图 → 平台尺寸…”可修改长、宽、高，范围 1～10000 mm；修改后保持已有模型的世界位置。
- 原点位于基板一角，X/Y 轴沿基板两边，Z 轴垂直向上；网格间距 10 mm。

## 示例模型

`asset/` 中提供以下 STL 文件，可用于测试导入、变换、阵列和切片：

| 文件 | 模型 |
| --- | --- |
| `cube_30mm.stl` | 立方体 |
| `sphere_30mm.stl` | 球体 |
| `cylinder_30mm.stl` | 圆柱体 |
| `cone_30mm.stl` | 圆锥体 |
| `torus_30mm.stl` | 圆环 |
| `pyramid_30mm.stl` | 棱锥 |

## 代码结构

| 文件 / 目录 | 职责 |
| --- | --- |
| `CMakePresets.json` | Windows Ninja 构建预设和 Qt 路径配置。 |
| `glSceneEditor/CMakeLists.txt` | 程序目标、依赖链接、资源复制和运行库部署。 |
| `main.cpp`、`welcome_dialog.*`、`recent_projects.*` | 程序入口、欢迎界面和最近工程。 |
| `project_file.*` | `.glproj` 工程读写、校验和原子保存。 |
| `scene_editor_window.*` | 菜单、工具栏、模型列表、信息面板、变换/阵列/切片对话框。 |
| `scene_viewport.*` | GLFW/OpenGL 视口、渲染、拾取、撤销记录和切片预览数据。 |
| `camera.h`、`camera_controller.*` | 相机状态、投影和鼠标键盘控制。 |
| `view_cube.*` | 视角立方体的绘制与交互。 |
| `base_plate.h`、`stl_model.h` | 基板、坐标轴和 STL 网格。 |
| `slice_engine.*` | 基于 PySLM C++ 的切片计算。 |
| `model_transform.h`、`model_names.h` | 模型变换和名称规则。 |
| `tests/` | 工程读写、界面流程和切片测试。 |
| `asset/shaders/`、`asset/icon/` | 着色器和工具栏图标资源。 |
| `3rd_party/` | 仓库内的第三方依赖。 |

## 依赖

GLFW、Dear ImGui、GLAD、GLM、Assimp、stb_image 和 ImGuizmo 的项目所需文件保存在 `3rd_party/` 中。

切片功能使用 `3rd_party/pyslm_cpp` 中的 PySLM C++ 库，需要保留其 `include/` 头文件以及 `debug/`、`release/` 下的导入库和 DLL（含 `geos`、`manifold` 等运行时依赖）。该库仅提供 Windows x64 版本，因此项目当前只支持 Windows x64 构建。Qt 和 MSVC 工具链需另行安装。

## 测试

默认启用 CTest，构建后运行：

```powershell
ctest --test-dir out/build/x64-debug --output-on-failure
```

- `project_roundtrip`：工程读写、旧版本兼容、损坏文件保护、撤销/回撤和模型变换
- `project_ui_workflow`：菜单、快捷键、保存流程、变换窗口、最近工程记录
- `slicing_engine`：切片层网格、轮廓、填充角度和路径长度

界面测试需要与主程序相同的 OpenGL 3.3 环境。只构建主程序时，可在配置阶段使用 `-DBUILD_TESTING=OFF`。
