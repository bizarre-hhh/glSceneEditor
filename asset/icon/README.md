# 工具栏图标

图标来源：[Lucide 官方仓库](https://github.com/lucide-icons/lucide)。
使用的源代码版本：`a04f228cd01185e09c188b7227b9600c08c565ec`。

保留 SVG 原始几何和 24 × 24 viewBox，只将 `currentColor` 描边改为 `#334155`，以便 Qt 正确显示。工具栏按 32 × 32 逻辑像素绘制，可适配高 DPI 屏幕；禁用状态由 Qt 生成。

| 按钮 | 图标文件 | 含义 |
| --- | --- | --- |
| 新建工程 | `file-plus.svg` | 文档加号 |
| 打开工程 | `folder-open.svg` | 打开的文件夹 |
| 保存工程 | `save.svg` | 软盘 |
| 另存为 | `save-pen.svg` | 带笔的软盘 |
| 打开 STL | `package-plus.svg` | 立方体加号 |
| 撤销 | `undo-2.svg` | 向左弯箭头 |
| 回撤 | `redo-2.svg` | 向右弯箭头 |
| 操作记录 | `rotate-ccw-clock.svg` | 历史时钟 |
| 重置视角 | `house.svg` | Home 视角 |
| 场景设置 | `settings.svg` | 齿轮 |
| 平台尺寸 | `ruler.svg` | 尺寸标尺 |

`toolbar_icons.qrc` 将图标和原始许可证嵌入程序，不依赖启动目录。修改 SVG 后重新构建即可更新图标。`source.json` 记录来源及改动；`LICENSE-Lucide.txt` 保留上游完整的 ISC / MIT 许可声明。
