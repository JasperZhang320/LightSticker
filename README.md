# LightSticker

LightSticker 是一个基于原生 Win32 C++（MSVC）的轻量级桌面层贴纸程序，窗口挂靠到 WorkerW 桌面层，而不是普通置顶窗口。

LightSticker is a lightweight native Win32 C++ desktop-layer sticker app attached to WorkerW (not a normal always-on-top window).

## 功能

- 启动后创建无边框贴纸窗口，挂靠 WorkerW 桌面层
- 默认不显示在任务栏 / Alt-Tab（`WS_EX_TOOLWINDOW`）
- 双击贴纸进入多行文本编辑（原生 `EDIT` 控件）
- `Esc` 结束编辑并保存
- 支持多贴纸（单进程多窗口）：`New` / `Duplicate` / `Delete`
- 支持锁定/解锁（锁定后禁止拖动、缩放和编辑）
- 内置主题：`Pale Yellow`（默认）/ `Miku` / `Transparent`，加上 `themes/` 目录里所有 `.json` 主题包
- 支持字体与字号：`Default` / `Consolas`，`Small` / `Medium` / `Large`
- 支持 `Ctrl + 鼠标滚轮` 快速调整字号档位
- 右键菜单提供 `Edit`、主题/字体/字号切换及 `Exit`
- 系统托盘图标：右键弹出菜单（New / Lock all / Re-pin / Save / Exit）
- 全局热键 `Ctrl+Alt+N`：在鼠标位置生成新贴纸
- `Alt+F4` 退出程序
- 退出时持久化文本与窗口位置尺寸，重启自动恢复（JSON 文件）
- 在 Win+D（显示桌面）后仍可见（桌面层窗口）
- 多显示器 / 高 DPI：声明 PerMonitor V2，跨屏拖动会重建字体尺寸
- 文字与背景使用 Direct2D + DirectWrite 渲染（圆角 + 整窗口透明度 + 抗锯齿文字）
- Explorer 重启后会自动重新挂靠 WorkerW（监听 `TaskbarCreated` 广播）

## 构建步骤（Windows 11 + MSVC）

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 运行方式

编译完成后运行：

```powershell
.\build\Release\LightSticker.exe
```

## 配置文件位置

启动时按以下顺序选择 JSON 配置：

1. 程序同目录的 `LightSticker.json`
2. 若同目录不可写，则回退到 `%LOCALAPPDATA%\LightSticker\settings.json`

首次运行如果检测到旧版的 `LightSticker.ini`（来自 0.1.x），会一次性读取其内容并写入 JSON；旧 `.ini` 会保留作为安全备份，不会被自动删除。

## 主题包（design tokens）

`themes/*.json` 里的每个文件是一个主题包，可以由 LLM 生成（参考 `themes/README.md` 里的工作流）。运行时按以下顺序加载：

1. `<EXE 同目录>/themes/*.json`
2. `%LOCALAPPDATA%\LightSticker\themes\*.json`（覆盖同 `id` 的内置主题）

加载到的主题会出现在右键 -> Theme 子菜单中。点选后会把 `bgColor`、`textColor`、`opacityPercent`、`cornerRadius` 写入当前贴纸；这些字段也持久化到 `settings.json`。

## 主机端测试

`tests/` 目录下有跨平台的小测试，便于在不进 Windows 的情况下验证 JSON / 主题包格式：

```bash
clang++ -std=c++17 -Wall -Wextra -Werror tests/json_min_test.cpp -o /tmp/jmt && /tmp/jmt
clang++ -std=c++17 -Wall -Wextra -Werror tests/theme_pack_test.cpp -o /tmp/tpt && /tmp/tpt
```
