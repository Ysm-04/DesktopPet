# QtDesktopPet

当前版本：**v0.3.0**。安装器、便携版和独立角色工坊分别提供完整运行依赖。

一个基于 **Qt 6 Widgets / C++17** 的 Windows 桌面宠物项目。它提供透明桌宠窗口、PNG 序列帧动画、多状态切换、托盘控制、AI 对话、AI 原生联网、本地提醒和天气查询等能力。

> 当前仓库仍处于活跃开发阶段。角色素材、图标和音效的再分发权限需要在正式公开发布前单独确认。

![Preview](resources/characters/shiroko/preview.png)

## Features

- 透明无边框桌宠窗口，支持拖动、置顶、托盘隐藏和单实例唤醒。
- 兼容 v1 PNG 与 v2 PNG/WebP/透明视频角色包的多状态动画：`idle`、`talk`、`think`、`sleep`、`happy`、`drag`、`error`。
- 角色包导入、切换、导出和用户角色目录管理。
- AI 对话：
  - OpenAI-compatible API
  - Google Gemini API
  - DeepSeek / Custom Provider 配置
  - 流式输出、请求取消、对话历史面板
  - Windows DPAPI 加密保存 API Key
- 联网模式：
  - 输入框“联网”开关
  - OpenAI Responses API Web Search 与 Chat Completions `web_search_options`
  - Google Gemini Google Search grounding（支持 `Google` 与 `Custom` 原生协议）
  - DeepSeek 官方 Web Tools（`deepseek_web_search`）
  - 支持为中转站 / Custom Provider 手动指定联网协议与假联网测试警示
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
- 外部编程 Agent 并行状态联动（只读状态副屏）：
  - 通过本地 Named Pipe（`QtDesktopPet.AgentStatus.v1`）与后台桥接程序 `QtDesktopPetBridge.exe` 接入 Codex、Antigravity、DSH 及自定义 CLI
  - 每种 Agent 一个胶囊（最多 4 种来源 + `+N`），详情按「Agent → 对话 → 命令/工具活动」分层，支持展开全部、隐藏对话、清除已结束和清空，刷新保持折叠与滚动位置
  - 分级角色台词轻气泡（`attention` / `all` / `silent`）与可选提示音，不重复发系统托盘通知，且不向外部 Agent 下发任务或审批权限
- Windows 发布脚本和 Inno Setup 安装器脚本。
- 可选角色呼吸、拖拽摆动、静态闭眼帧和状态角标，以及标准/低功耗两种运行模式。
- 独立角色工坊 GUI/CLI：PNG、透明原片或绿幕素材转 WebP/透明视频，支持首帧对照、顺序队列和取消。

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
├── examples/
│   └── agent_bridge/          # Hook/plugin templates and self-test script for external coding agents
├── installer/                 # Inno Setup script
├── resources/
│   ├── characters/            # Built-in character packages
│   ├── icons/
│   └── sounds/
├── src/
│   ├── agent_bridge/          # Read-only external agent status IPC server, codec, registry & bridge CLI
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

普通用户请下载完整安装包或便携 ZIP，参阅 [安装与使用说明](使用说明.md)。发布包包含运行库和插件，**无需安装 Qt、Visual Studio、CMake、Python 或单独的 Node.js**。下列环境只供源码开发使用；外部 Agent 客户端需自行安装配置。

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

For MSVC builds, run the package script from a Visual Studio developer shell or pass `-MsvcRuntimeDir`. Use `-WindeployQtPath` to select the same Qt Kit as the build. The script includes both executables, Qt plugins, app-local compiler runtimes, `examples/agent_bridge/` and the offline [user guide](使用说明.md). It checks normal/delay DLL imports and bridge startup with an empty PATH before creating a ZIP or installer; results are saved in `deployment-report.json`. Missing dependencies stop packaging.

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

## External Agent Status Bridge

QtDesktopPet includes an optional read-only status bridge (`QtDesktopPet.AgentStatus.v1`) via `QtDesktopPetBridge.exe`. External coding agents (Codex, Google Antigravity, DeepSeek Harness / DSH, or custom CLI tools) can report status updates to display compact status capsules and character dialogue bubbles, while QtDesktopPet never sends tasks back to external agents or approves tool permissions on their behalf.

See [examples/agent_bridge/README.md](examples/agent_bridge/README.md) for templates and the self-test script (`test_bridge.ps1`).

配置步骤：

1. 启动桌宠，在 **设置 → 应用** 启用联动和底部胶囊，先点「测试联动」。
2. **Codex / Antigravity**：在对应行点击「一键接入」，确认后自动备份并合并 Hook。Codex 如提示 Hook 信任，需用户批准；必要时重启客户端。`CODEX_HOME` 自定义目录应与两端实际环境一致。
3. **DSH**：点「打开模板目录」，按 [DSH 配置步骤](examples/agent_bridge/README.md) 在个人 profile 的 `cordis.patch.yml` 中追加插件插入项，填写真实插件文件 URL 与桥接绝对路径，然后重启 DSH。插件使用 DSH 自带 Node，不需要 npm 安装。
4. 在各端执行只读工具操作，点击胶囊查看对应对话和活动。每种 Agent 只有一个胶囊，工具次数不会叠加为对话数。

Codex 手动停止输出需要客户端支持并信任新版模板中的 `Interrupt` Hook；已接入用户升级后需从设置页确认修复配置。桌面版与独立 CLI 版本可能不同，旧版本收不到中断时只能等 TTL 标为“待同步”。版本边界与复测步骤见随包 `examples/agent_bridge/README.md`。

完整发布目录必须一起保留；移动目录后修复 Hook 路径与 DSH 配置，Codex 可能需重新信任。详情仅保存本次运行的活动摘要，每对话最多 50 条、默认展示最近 5 条，最近对话约保留 10 分钟。桌宠不代替客户端审批权限。卸载前请解除接入，安装器不会擅自修改第三方配置。更多版本、隐私与故障注意事项见离线指南。

## Web Mode

Web mode is an AI-native conversation feature, not a search engine scraper.

Supported in `auto` mode:

- OpenAI official API (`api.openai.com`) with Responses API Web Search.
- DeepSeek official API (`api.deepseek.com`) with `deepseek_web_search`.
- Google Gemini protocol (`google-generative-language`, including `Google` and `Custom` Gemini protocol configs) with Google Search grounding.

Supported via explicit protocol selection in Settings -> Web Mode (for relays and `Custom` OpenAI-compatible endpoints):

- `OpenAI Responses` (`/v1/responses` + `web_search`)
- `OpenAI Chat` (`/chat/completions` + `web_search_options`, for legacy relays or search-preview models)
- `DeepSeek Web Tools` (`/chat/completions` + `deepseek_web_search`)
- `Gemini Grounding` (`google_search`, requires `google-generative-language`)

Unsupported or mismatched configurations show an explicit message instead of falling back to unreliable search-page scraping, and "Test Web Mode" warns when a relay returns HTTP 200 without detectable server-side search activity.

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

The export excludes internal docs, reports, build outputs, release packages, local config, logs and Git metadata. Only the reviewed user guides `docs/character-studio.md` and `docs/character-package-v2.md` are included.

## License

Source code is released under the MIT License. See [LICENSE](LICENSE).

Character art, icons, sounds and other media assets may have separate copyright requirements. Confirm asset licensing before public redistribution.

角色作者可参考 [使用说明中的角色包配置](使用说明.md)；源码仓库另有 `examples/character_v2/` 模板。现有 v1 包无需迁移；v2 可按状态混用 PNG、Animated WebP 与 Alpha-Matte H.264 视频。视频需明确 RGB/灰度 Alpha 的拼接方向，首版静音；完整示例见 `examples/character_alpha_video/`。发布包附带 WebP/视频插件和运行库，无需安装开发环境或 FFmpeg 命令行。

角色作者可使用独立角色工坊，支持序列帧/透明原片/绿幕素材转换和轻动效声明，完整作者包自带运行依赖。使用方式见 [角色工坊](docs/character-studio.md)，规范见 [v2 角色包](docs/character-package-v2.md)。

v0.3.0 的更新和验收边界见 [CHANGELOG](CHANGELOG.md)。短时回归不能代替长期稳定性；干净 Windows、安装升级卸载及完整人工视觉验收尚未完成。安装器未签名，Windows SmartScreen 可能提示未知发布者。
