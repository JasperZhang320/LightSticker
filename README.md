# LightSticker

LightSticker 是一个基于原生 Win32 C++（MSVC）的轻量级桌面层贴纸程序，窗口挂靠到 WorkerW 桌面层，而不是普通置顶窗口。

LightSticker is a lightweight native Win32 C++ desktop-layer sticker app attached to WorkerW (not a normal always-on-top window).

## 功能

- 启动后创建无边框贴纸窗口，挂靠 WorkerW 桌面层
- 默认不显示在任务栏 / Alt-Tab（`WS_EX_TOOLWINDOW`）
- 双击贴纸进入多行文本编辑（原生 `EDIT` 控件）
- `Esc` 结束编辑并保存
- 右键菜单提供 `Edit` / `Exit`
- `Alt+F4` 退出程序
- 退出时持久化文本与窗口位置尺寸，重启自动恢复（INI 文件）
- 在 Win+D（显示桌面）后仍可见（桌面层窗口）

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

默认配置文件路径：

`%LOCALAPPDATA%\LightSticker\settings.ini`

若无法获取 LocalAppData，会回退到程序同目录的 `LightSticker.ini`。
