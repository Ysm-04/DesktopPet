# QtDesktopPet

当前版本：**v0.3.0**。安装器、便携版和独立角色工坊分别提供完整运行依赖。

QtDesktopPet 是一个基于 **Qt 6 Widgets / C++17** 的 Windows 桌面宠物，也是 **Codex、Google Antigravity、DeepSeek Harness（DSH）等编程 Agent 的本地状态展示窗口**。多个 Agent 同时工作时，每种来源显示一个胶囊，展开后按「Agent → 对话 → 命令与工具活动」查看进展，并通过角色状态和气泡提示待确认、暂停、完成或异常。桌宠还提供 AI 对话、AI 原生联网、本地提醒、天气查询、多种角色动画，以及制作角色素材的独立角色工坊。

> 当前项目仍在持续完善。角色素材、图标和音效的授权独立于源码许可证，再分发前请确认对应权限。

![桌宠预览](resources/characters/shiroko/preview.png)

## 主要功能

- 透明无边框桌宠窗口，支持拖动、置顶、托盘隐藏和单实例唤醒。
- 兼容 v1 PNG 与 v2 PNG、动态 WebP、透明视频角色包，支持待机、说话、思考、睡眠、开心、拖拽、异常等状态。
- 角色包导入、切换、导出和用户角色目录管理。
- AI 对话：兼容 OpenAI 的接口、Google Gemini、DeepSeek 和自定义服务商；支持流式输出、取消请求和对话历史，在 Windows 上使用 DPAPI 加密保存 API 密钥。
- AI 原生联网：支持 OpenAI 搜索工具、Google 搜索增强及 DeepSeek 官方联网工具；中转站可手动选择协议，并测试是否真正触发服务端搜索。
- 本地提醒：一次性或每天、每周、每月重复提醒，支持音效导入与试听；桌宠隐藏或 AI 忙碌时使用系统通知。
- 天气查询：天气预报、城市名解析、默认城市及公网 IP 定位回退，支持数据源故障切换和定位缓存。
- 多 Agent 状态联动：
  - 通过本地命名管道 `QtDesktopPet.AgentStatus.v1` 和桥接程序 `QtDesktopPetBridge.exe` 接入 Codex、Antigravity、DSH 或自定义命令行工具。
  - 每种 Agent 一个胶囊，详情按「Agent → 对话 → 命令与工具活动」分层；支持展开全部、隐藏对话、清理已结束记录，并保持折叠和滚动位置。
  - 可选择气泡等级和提示音；只展示上报状态，不向 Agent 下发任务或代替用户审批权限。
- 可选角色轻动效：呼吸、有限拖拽摆动、专用静态闭眼帧和状态角标，提供标准与低功耗两种运行模式。
- 独立角色工坊：将 PNG 序列、透明原片或绿幕素材转换为动态 WebP、透明视频或单 PNG 角色，支持首帧对照、生成队列和取消。
- Windows 发布打包脚本及简体中文安装器。

## 下载与开始使用

普通用户可在本仓库的版本发布页面下载对应文件：

| 文件 | 用途 |
| --- | --- |
| `QtDesktopPet-0.3.0-windows-x64-setup.exe` | 安装版，按向导安装桌宠 |
| `QtDesktopPet-0.3.0-windows-x64.zip` | 便携版，完整解压后运行 `QtDesktopPet.exe` |
| `QtDesktopPet-CharacterStudio-0.3.0-windows-x64.zip` | 独立角色工坊，供制作或转换角色素材使用 |
| `SHA256SUMS.txt` | 下载文件的 SHA256 校验值 |

发布包自带运行库和插件，**无需安装 Qt、Visual Studio、CMake、Python、FFmpeg 或单独的 Node.js**。外部 Agent 客户端需自行安装和配置。不要在压缩包内直接运行程序，也不要只复制单个 EXE。

首次运行后，右键桌宠打开设置；托盘图标可显示、隐藏或退出桌宠。完整步骤见 [安装与使用说明](使用说明.md)。

## Agent 联动配置

桌宠通过本地桥接程序接收外部 Agent 的状态，显示胶囊、对话详情和角色气泡。它只展示状态，任务执行与权限审批仍在原客户端完成。

1. 启动桌宠，在 **设置 → 应用** 启用联动和底部胶囊，先点「测试联动」。
2. **Codex / Antigravity**：在对应行点击「一键接入」，确认后自动备份并合并 Hook 配置。Codex 如提示信任 Hook，需用户批准；必要时重启客户端。自定义 `CODEX_HOME` 时，应确保客户端和桌宠使用同一目录。
3. **DSH**：点「打开模板目录」，按 [DSH 配置步骤](examples/agent_bridge/README.md) 在个人配置的 `cordis.patch.yml` 中追加插件项，填写实际插件文件地址与桥接程序绝对路径，然后重启 DSH。插件使用 DSH 自带 Node，无需执行 `npm install`。
4. 在各端执行一次查看目录等只读工具操作，点击胶囊查看对话及活动。每种 Agent 只有一个胶囊，工具调用次数不会叠加为对话数。

Codex 手动停止输出需要客户端支持并信任 `Interrupt` Hook。升级后，已接入用户应在设置页确认修复配置。桌面版与独立命令行版可能不同；旧客户端没有中断事件时，桌宠只能在状态超时后标为「待同步」，无法保证立即显示暂停。

请保留完整发布目录。移动目录后需修复 Hook 路径及 DSH 配置，Codex 可能要求重新信任。详情仅保存本次运行中的活动摘要，每个对话最多 50 条、默认展示最近 5 条，最近对话约保留 10 分钟。隐藏或清空记录不会停止外部任务；卸载前请解除接入，安装器不会自动改写第三方配置。

模板、自检脚本 `test_bridge.ps1`、版本边界和故障排查见 [Agent 离线接入指南](examples/agent_bridge/README.md)。

## 角色工坊：从素材到桌宠

角色工坊是独立程序，普通桌宠安装包不包含它。请另外下载并完整解压 `QtDesktopPet-CharacterStudio-0.3.0-windows-x64.zip`，运行 `QtDesktopPetCharacterStudio.exe`；不要只拷贝 EXE，也不要删除同目录的 `encoder/`、DLL 和插件。

以一组透明 PNG 待机动画为例：

1. **准备素材**：将同一动作的图片放在一个文件夹中，使用 `000001.png`、`000002.png` 等补零文件名；建议画布尺寸一致。
2. **选择素材**：在「1 · 素材」点击「选择序列 / 角色目录」，选中图片文件夹。若使用单张 PNG、透明原片或绿幕视频，点击「选择文件」。
3. **设置输出**：为新角色填写便于区分的「新角色 ID」，例如 `my-first-pet`；点击「选择父目录」，确认「新输出目录」尚不存在，并且与素材目录互不包含。先填写 ID 再选父目录；之后修改 ID 时也要检查输出路径。
4. **设置格式**：在「2 · 输出与轻动效」选择「Animated WebP · 无损编码」，最大逻辑边可先保留 512，输出帧率保留自动；「PNG 序列源帧率」填写素材实际帧率，例如 12。透明素材无需开启绿幕抠像。
5. **检查预览**：在「3 · 预览与生成」点击「更新对照预览」，切换透明棋盘、浅色和深色背景，检查原始与处理后的首帧。
6. **生成角色**：点击「加入生成队列」，等待显示完成。队列按顺序执行；「取消当前 / 清空等待队列」会取消未完成任务，已成功生成的目录保留。
7. **导入桌宠**：打开桌宠 **设置 → 角色 → 导入角色文件夹**，选择生成目录，也就是含有 `character.json` 的文件夹。导入通过后，在「当前角色」中选择它并保存设置，再检查完整播放效果。

新图片或视频首先生成一个 `idle`（待机）状态，不会自动补出 `talk`（说话）、`think`（思考）等动作。需要多状态角色时，先按角色包规范准备好各状态，再选择已有角色目录，只转换需要处理的状态。

不同用途可选择不同输出：

| 用途 | 工坊选项与注意事项 |
| --- | --- |
| 透明短动画 | 选择「Animated WebP · 无损编码」，适合从 PNG 序列转换 |
| 跳舞等复杂动作 | 选择「Alpha-Matte MP4 · H.264 CRF12」；视频默认静音，保留原始母版并检查透明边缘 |
| 静态立绘 | 选择「单 PNG + 轻动效」，按需开启呼吸、拖拽摆动和状态角标 |
| 静态角色眨眼 | 另提供与睁眼图同画布的完整闭眼 PNG，仅支持静态 `idle` |
| 绿幕视频 | 启用绿幕抠像，调整背景取色、相似度、过渡及去绿边，再检查对照预览 |
| 已有角色包 | 在「已有包：选择要转换的状态」中勾选目标状态；未选素材、人设、台词和锚点保留 |

预览只展示首帧；WebP 的无损针对缩放、抠像后的结果，视频编码可能造成有损边缘。导入后仍应检查完整动作、透明边缘和气泡位置。转换失败时可点「显示诊断记录」查看错误摘要；详细参数、命令行用法及常见问题见 [角色工坊使用说明](docs/character-studio.md)。

## 角色包与动画

内置角色位于 `resources/characters/<角色ID>/`，典型 PNG 角色目录如下：

```text
角色目录/
├── character.json             # 角色清单
├── preview.png                # 预览图
├── idle/                      # 待机
├── talk/                      # 说话
├── think/                     # 思考
├── sleep/                     # 睡眠
├── happy/                     # 开心
├── drag/                      # 拖拽
└── error/                     # 异常
```

用户导入的角色复制到用户数据目录，不写入安装目录。现有 v1 包无需迁移；v2 可以让不同状态分别使用 PNG、动态 WebP 或带透明遮罩的 H.264 视频。视频需明确左右或上下拼接方向，目前统一静音。

手工配置见 [使用说明中的角色包配置](使用说明.md)，完整字段和限制见 [v2 角色包规范](docs/character-package-v2.md)。源码仓库提供 `examples/character_v2/` 模板、`examples/character_webp/`、`examples/character_alpha_video/` 及 `examples/character_effects/` 示例。

## 联网模式

联网模式使用 AI 服务商提供的原生搜索能力。

自动模式可识别以下配置：

- OpenAI 官方接口 `api.openai.com`：使用 Responses 接口的 `web_search` 搜索工具。
- DeepSeek 官方接口 `api.deepseek.com`：使用 `deepseek_web_search`。
- Google Gemini 协议 `google-generative-language`：使用 Google 搜索增强，也支持采用该协议的自定义服务商配置。

中转站和自定义兼容接口可在 **设置 → 联网模式** 中手动指定协议。下列名称与程序中的选项对应：

| 协议选项 | 接口与能力 |
| --- | --- |
| `OpenAI Responses` | `/v1/responses` 与 `web_search` |
| `OpenAI Chat` | `/chat/completions` 与 `web_search_options`，用于兼容旧式中转站或搜索预览模型 |
| `DeepSeek Web Tools` | `/chat/completions` 与 `deepseek_web_search` |
| `Gemini Grounding` | `google_search`，需使用 `google-generative-language` 协议 |

不支持或不匹配的配置会给出明确提示。「测试联网模式」还会检查服务端搜索证据，避免将仅返回成功状态码、却忽略搜索参数的中转站误判为联网可用。

## AI 对话与隐私

AI 对话和联网请求发送到用户配置的服务商接口。API 密钥与身份验证请求头不写入日志；日志应避免记录完整用户消息和完整错误正文。在 Windows 上，API 密钥通过可用的 DPAPI 加密保存。

第三方兼容接口与中转站由用户选择，其隐私行为需自行评估。反馈问题时，请提供脱敏错误摘要，避免公开密钥、个人配置和完整对话。

## 使用边界

- 聊天功能不提供本地文件读取、搜索、增删改或移动操作，也不将聊天内容转换为系统命令。
- 本地应用启动与系统命令执行未作为聊天能力开放。
- Windows 开机启动功能只管理本程序在当前用户 `Run` 项中的记录。
- Agent 联动仅接收状态；外部 Agent 的执行和权限管理仍由原客户端负责。
- 角色导入、素材转换等操作由用户在对应界面中明确选择本地目录后执行。

## 运行数据

运行配置和用户数据存放在 Qt 标准用户目录 `QStandardPaths::AppConfigLocation` 与 `QStandardPaths::AppDataLocation` 中，主要文件包括：

| 文件 | 内容 |
| --- | --- |
| `ai_config.json` | AI 服务配置 |
| `app_config.json` | 应用设置 |
| `conversation_history.json` | 对话历史 |
| `reminders.json` | 提醒记录 |
| `weather_config.json` | 天气配置 |
| `web_config.json` | 联网模式配置 |

日志按大小轮转，位置为 `QStandardPaths::AppConfigLocation/logs/QtDesktopPet.log`。安装、升级和卸载注意事项见 [使用说明](使用说明.md)。

## 支持平台与技术选型

主要目标平台为 Windows 10 / Windows 11 64 位。项目中保留部分跨平台基础代码；托盘通知、开机启动与安装器目前优先面向 Windows。

主要使用 C++17、Qt 6 Widgets 界面组件、Qt 6 Network 网络模块、Qt 6 Multimedia 多媒体模块、CMake、JSON 配置和 Inno Setup 安装器。

## 源码目录

```text
.
├── CMakeLists.txt
├── main.cpp
├── docs/                      # 公开的角色规范和工坊用户指南
├── examples/                  # Agent 接入及角色素材示例
├── installer/                 # 简体中文安装器脚本
├── resources/
│   ├── characters/            # 内置角色包
│   ├── icons/                 # 图标
│   └── sounds/                # 音效
├── src/
│   ├── agent_bridge/          # Agent 状态通道、消息编解码、会话管理和桥接程序
│   ├── ai/                    # AI 服务适配及对话状态
│   ├── assistant/             # 意图识别与命令分派
│   ├── character/             # 角色包加载和媒体播放
│   ├── config/                # 配置保存
│   ├── notification/          # 通知分派
│   ├── reminder/              # 提醒解析、保存、调度及音效
│   ├── state/                 # 角色状态机
│   ├── system/                # Windows 开机启动集成
│   ├── tray/                  # 系统托盘
│   ├── ui/                    # 界面与桌宠主窗口
│   ├── util/                  # 通用工具
│   ├── weather/               # 天气服务
│   └── web/                   # AI 原生联网
└── tools/                     # 作者工具源码、依赖构建、打包和公开导出脚本
```

## 源码构建

以下环境仅供源码开发使用，普通用户下载完整发布包即可。

建议使用 Qt 6.5 及以上版本、CMake 3.20 及以上版本、Ninja，以及匹配 Qt 构建套件的编译器。本轮 Windows 发布包实际采用 Qt 6.5.3 / MSVC x64；下面保留 MinGW 构建命令示例，路径需按本机安装位置调整，不代表已完成该套件的发布验收。

```powershell
cmake -S . -B build/mingw-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.5.3/mingw_64 `
  -DCMAKE_C_COMPILER=D:/Qt/Tools/mingw1120_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1120_64/bin/g++.exe

cmake --build build/mingw-release
```

也可以用 Qt Creator 打开 `CMakeLists.txt`，选择匹配的构建套件。主工程默认同时构建桌宠、桥接程序及角色工坊；设置 `PET_BUILD_CHARACTER_STUDIO=OFF` 可仅构建桌宠和桥接程序。

## 发布打包

先构建发布模式程序，并用 `tools/build_webp_plugin.ps1` 准备与 Qt 版本和编译器匹配的 WebP 依赖包。脚本参数见该文件，默认依赖输出位于 `build/webp-runtime/bundle`。完成后执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe
```

生成安装器时增加 `-BuildInstaller`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe `
  -BuildInstaller
```

MSVC 构建需在 Visual Studio 开发者命令环境中运行，或通过 `-MsvcRuntimeDir` 指定运行库。使用 `-WindeployQtPath` 选择同一 Qt 构建套件中的部署工具；非默认位置的 WebP 依赖通过 `-WebpRuntimeDir` 指定。

脚本复制桌宠、桥接程序、Qt 插件、编译器运行库、Agent 模板及离线指南，在空开发环境路径下检查依赖和实际媒体解码，结果写入 `deployment-report.json`。缺少依赖时中止打包。安装器的桌面快捷方式和开机启动选项默认不勾选。

作者工坊通过 `tools/package_character_studio.ps1` 单独打包；固定编码器的构建入口为 `tools/build_character_encoder.ps1`，来源清单与配方位于 `tools/author/`。作者包必须保留编码器许可证、对应源码和构建材料，不能将普通桌宠包当作作者包。

## 公开仓库导出

开发目录包含内部计划、测试记录与构建产物，使用以下脚本生成公开副本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_github_export.ps1 `
  -OutputDir D:\DesktopPet-github-export
```

导出排除内部文档、报告、构建产物、安装包、本地配置、日志和 Git 元数据。`docs/` 中仅放行已审核的 `character-studio.md` 与 `character-package-v2.md` 两份用户文档。

## 版本状态与许可证

v0.3.0 的更新和验收边界见 [更新记录](CHANGELOG.md)。短时回归不能代替长期稳定性；干净 Windows、安装升级卸载及完整人工视觉验收尚未完成。安装器未签名，Windows SmartScreen 可能提示未知发布者。

源码采用 MIT 许可证，详见 [许可证](LICENSE)。角色素材、图标、音效及随包第三方组件分别遵循各自授权与许可。
