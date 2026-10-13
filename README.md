# QtDesktopPet

一个基于 **Qt 6 Widgets / C++17** 的 Windows 桌面宠物项目。它提供透明桌宠窗口、PNG 序列帧动画、多状态切换、托盘控制、AI 对话、AI 原生联网、本地提醒和天气查询等能力。

> 当前仓库仍处于活跃开发阶段。角色素材、图标和音效的再分发权限需要在正式公开发布前单独确认。

![Preview](resources/characters/shiroko/preview.png)

## Features

- 透明无边框桌宠窗口，支持拖动、置顶、托盘隐藏和单实例唤醒。
- 多状态 PNG 序列帧动画：`idle`、`talk`、`think`、`sleep`、`happy`、`drag`、`error`。
- 角色包导入、切换、导出和用户角色目录管理。
- AI 对话：
  - OpenAI-compatible API
  - Google Gemini API
  - DeepSeek / Custom Provider 配置
  - 流式输出、请求取消、对话历史面板
  - Windows DPAPI 加密保存 API Key
- 联网模式：
  - 输入框“联网”开关
  - OpenAI 官方 Responses API Web Search
  - Gemini Google Search grounding
  - DeepSeek / Custom 默认提示不支持或无法确认原生联网
- 本地提醒：
  - 一次性提醒
  - 每天 / 每周 / 每月重复提醒
  - 提醒音效导入、试听、切换
  - 桌宠可见时气泡提示，隐藏或 AI 忙时系统通知
- 天气查询：
  - Open-Meteo Forecast API
  - Open-Meteo Geocoding API
  - 默认城市、可信同名候选提示，以及常用海外城市中文名解析
  - 公网 IP 定位依次尝试 ipapi.co 和 ipwho.is，单个服务故障时自动切换
- Windows 发布脚本和 Inno Setup 安装器脚本。

## Platform

当前主要目标平台是 Windows 10 / Windows 11。

项目中已有部分跨平台基础代码，但托盘通知、开机自启动和安装器体验目前按 Windows 优先实现。

## Tech Stack

- C++17
- Qt 6 Widgets
- Qt 6 Network
- Qt 6 Multimedia
- CMake
- JSON 配置
- PNG 序列帧动画
- Inno Setup

## Repository Layout

```text
.
├── CMakeLists.txt
├── main.cpp
├── installer/                 # Inno Setup script
├── resources/
│   ├── characters/            # Built-in character packages
│   ├── icons/
│   └── sounds/
├── src/
│   ├── ai/                    # AI providers and conversation state
│   ├── assistant/             # Intent routing and command dispatch
│   ├── character/             # Character package loading and animation
│   ├── config/                # Config persistence
│   ├── notification/          # Notification dispatch
│   ├── reminder/              # Reminder parser/store/scheduler/sounds
│   ├── state/                 # Pet state machine
│   ├── system/                # Windows startup integration
│   ├── tray/                  # System tray controller
│   ├── ui/                    # Widgets and main pet window
│   ├── util/
│   ├── weather/
│   └── web/                   # AI-native web mode
└── tools/                     # Packaging and public-export scripts
```

## Build

Recommended environment:

- Qt 6.5+
- CMake 3.20+
- Ninja
- MinGW 11.2.0 or a configured Qt MSVC Kit

Example with MinGW:

```powershell
cmake -S . -B build/mingw-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.5.3/mingw_64 `
  -DCMAKE_C_COMPILER=D:/Qt/Tools/mingw1120_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1120_64/bin/g++.exe

cmake --build build/mingw-release
```

Qt Creator users can open `CMakeLists.txt` directly and build with a matching Qt Kit.

## Package

After building a Release executable, package it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe
```

To generate the Inno Setup installer:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe `
  -BuildInstaller
```

For MSVC builds, run the package script from a Visual Studio developer shell or pass `-MsvcRuntimeDir`. The script deploys the x64 MSVC runtime beside the application and fails early when the required runtime files are unavailable.

The installer supports optional desktop shortcut creation and optional Windows startup launch. Both are disabled by default.

## Runtime Data

Runtime configuration and user data are stored under Qt standard user directories:

- `QStandardPaths::AppConfigLocation`
- `QStandardPaths::AppDataLocation`

Examples:

- AI config: `ai_config.json`
- App config: `app_config.json`
- Conversation history: `conversation_history.json`
- Reminders: `reminders.json`
- Weather config: `weather_config.json`
- Web mode config: `web_config.json`

The app writes rotating logs under:

```text
QStandardPaths::AppConfigLocation/logs/QtDesktopPet.log
```

## AI And Privacy

QtDesktopPet only sends chat content to the AI endpoint configured by the user.

Important notes:

- API keys are not logged.
- Authorization headers are not logged.
- Full user messages and full error bodies should not be logged.
- On Windows, API keys are saved with DPAPI when available.
- Third-party compatible APIs and proxy services are controlled by the user; this project cannot guarantee their privacy behavior.

## Safety Boundaries

- The application does not read, search, create, modify, move, or delete local files through chat.
- It does not launch local applications or convert chat text into shell commands or scripts.
- Windows startup integration only manages this application's current-user `Run` entry.
- AI and web requests are sent only to providers configured by the user.

## Web Mode

Web mode is an AI-native conversation feature, not a search engine scraper.

Supported:

- OpenAI official API with Responses API Web Search.
- Google Gemini API with Google Search grounding.

Not treated as supported native web access:

- DeepSeek official API
- Custom OpenAI-compatible endpoints
- Third-party relay APIs

Unsupported providers show an explicit message instead of falling back to unreliable search-page scraping.

## Character Packages

Built-in characters are placed under:

```text
resources/characters/<characterId>/
```

A character package contains:

```text
character.json
preview.png
idle/
talk/
think/
sleep/
happy/
drag/
error/
```

User-imported characters are copied to the user's app data directory instead of the installation directory.

## Public Export

This development workspace may contain internal planning and test documents. To create a clean public GitHub export, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_github_export.ps1 `
  -OutputDir D:\DesktopPet-github-export
```

The export excludes internal docs, reports, build outputs, release packages, local config, logs and Git metadata.

## License

Source code is released under the MIT License. See [LICENSE](LICENSE).

Character art, icons, sounds and other media assets may have separate copyright requirements. Confirm asset licensing before public redistribution.
