# QtDesktopPet

QtDesktopPet 是一个基于 Qt Widgets / C++17 的 Windows 桌面宠物项目，当前已具备多状态 PNG 帧动画、托盘控制、角色包导入与切换、用户自定义大模型对话、设置面板和 Windows 发布打包能力。项目现阶段重点是完善稳定性、性能回归、角色管理和发布体验。

## 当前状态

已实现：

- 透明无边框桌宠窗口
- 鼠标拖动
- 右键菜单退出和状态测试
- 置顶切换
- `resources/characters/shiroko` 默认角色包读取
- PNG 序列帧动画播放
- `idle` / `talk` / `think` / `sleep` / `happy` / `drag` / `error` 状态
- 托盘显示、隐藏、退出
- 单实例限制，重复启动会唤醒已有实例
- 隐藏时暂停动画，显示时恢复动画
- 保存窗口位置、置顶、缩放和性能设置
- 文件日志和基础轮转
- 设置窗口按当前屏幕居中弹出
- 应用设置：缩放、性能模式、隐藏暂停、懒加载
- 状态级动画预热和 LRU 缓存卸载
- AI Provider 分组配置
- 设置页内 AI 连通性测试
- Windows DPAPI 加密保存 API Key
- 非 Windows 环境经用户确认后明文保存 API Key
- OpenAI Compatible 聊天请求
- SSE 流式输出
- 聊天输入框
- AI 回复气泡
- 对话历史面板
- 内存历史上限和可选本地历史保存
- AI 请求取消和对话清空
- Google Gemini 原生聊天请求
- 角色文件夹导入和角色切换
- 删除用户导入角色
- Windows 发布打包脚本和 Inno Setup 安装器脚本
- Windows GUI 子系统，Release exe 双击不弹控制台窗口

尚未实现：

- 角色导出和更完整的管理界面
- 对话历史导出/管理界面
- 长期性能压测记录
- 发布包实机安装/卸载验证

## 技术栈

- C++17
- Qt 6 Widgets
- Qt 6 Network
- CMake
- PNG 图片序列帧
- JSON 配置文件
- Windows 10 / Windows 11 优先

## 构建

推荐环境：

- Qt 6.5.3
- CMake 3.20+
- Ninja
- MinGW 11.2.0 或已配置好的 Qt MSVC Kit

MinGW 示例：

```powershell
cmake -S . -B build/mingw-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.5.3/mingw_64 `
  -DCMAKE_C_COMPILER=D:/Qt/Tools/mingw1120_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1120_64/bin/g++.exe

cmake --build build/mingw-debug
```

如果使用 Qt Creator，也可以直接打开根目录 `CMakeLists.txt`，选择合适的 Qt Kit 后构建。

## 应用图标

当前应用图标位于：

```text
resources/icons/app_icon.ico
resources/icons/app_icon_1024.png
```

`app_icon.ico` 用于窗口图标、托盘图标和 Windows exe 资源图标；托盘图标加载失败时会回退到默认角色包的 `preview.png`。`app_icon_1024.png` 作为高分辨率源图保留。
运行时会优先读取可执行文件同级的 `resources/icons/`，找不到时回退到源码目录下的 `resources/icons/`。Windows exe 图标需要重新构建后生效。

## 角色包

当前默认角色包位于：

```text
resources/characters/shiroko/
```

内置角色包按 `resources/characters/<characterId>/` 组织。用户导入的角色包不会写入安装目录，而是复制到用户数据目录：

```text
QStandardPaths::AppDataLocation/characters/<characterId>/
```

角色包基本结构：

```text
resources/characters/shiroko/
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

当前素材版本为 `2.1.0-stable`，所有帧使用 512x512 透明画布。当前实现会读取当前角色包的各状态配置，并按 `character.json` 中的 FPS 播放。
运行时会合并内置角色和用户导入角色；内置资源优先读取可执行文件同级的 `resources/characters/`，找不到时回退到源码目录下的 `resources/characters/`。

角色导入：

- 只支持导入本地文件夹，不支持 zip
- 导入前先验证源文件夹；验证失败只弹窗提示，不复制、不创建、不覆盖文件
- 验证通过后复制到用户数据目录
- 角色 id 优先读取 `character.json` 的 `id`；为空时使用文件夹名
- 角色 id 只允许 ASCII 字母、数字、点、下划线和短横线，且不能以点开头或结尾
- 用户角色同名时会询问是否覆盖
- 内置角色 id 不能被导入包覆盖
- 验证要求：`character.json` 可解析、id 安全、存在 `idle` 和 `defaultState`、状态路径安全、fps 合法、每个声明状态至少有一张可读 PNG
- 如果 `base.anchorY + bubble.offsetY` 计算出的气泡锚点明显偏低，导入时会提示用户检查配置，但不强制阻止导入
- 只允许删除用户导入角色；选择内置角色删除时只会提示“不能删除内置角色”，不做文件操作

懒加载现状：

- `enableLazyLoad=true` 时，启动阶段只建立状态到帧路径的索引
- 某个状态首次播放时加载该状态的 PNG 帧
- 启动后会在主线程按批次预热常用状态，避免一次性加载全部帧
- 已加载状态按状态级 LRU 策略管理，超过动画缓存上限时卸载非保护状态
- 单轮预热不会反复重新加载刚被 LRU 卸载的状态，避免缓存上限较低时出现加载/卸载循环
- 隐藏到托盘时可释放非保护动画缓存
- `enableLazyLoad=false` 时仍保持启动阶段加载全部状态帧的兼容行为

## 配置和日志

应用配置保存到 Qt 标准配置目录：

```text
QStandardPaths::AppConfigLocation/app_config.json
```

配置损坏时会备份为带时间戳的文件：

```text
app_config.broken.yyyyMMdd-HHmmss.json
ai_config.broken.yyyyMMdd-HHmmss.json
conversation_history.broken.yyyyMMdd-HHmmss.json
```

日志输出到文件，不输出到控制台：

```text
QStandardPaths::AppConfigLocation/logs/QtDesktopPet.log
```

如果 Qt 无法取得标准配置目录，则回退到当前工作目录下的 `logs/QtDesktopPet.log`。

日志轮转规则：

- 单个日志文件超过 2MB 时轮转
- 最多保留 3 个旧日志文件
- 文件名为 `QtDesktopPet.log.1`、`QtDesktopPet.log.2`、`QtDesktopPet.log.3`

## 发布打包

仓库提供 Windows Release 打包脚本。脚本不负责 CMake 构建，先手动完成 Release 构建，再把 exe 路径传给脚本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 -ExePath build/release/QtDesktopPet.exe
```

脚本会生成目录包和 zip：

```text
dist/QtDesktopPet-<version>-windows-x64/
dist/QtDesktopPet-<version>-windows-x64.zip
```

发布目录包含：

```text
QtDesktopPet.exe
Qt runtime
resources/characters/
resources/icons/
LICENSE
README.md
```

脚本会调用 `windeployqt.exe` 收集 Qt 运行库。若当前 PATH 找不到 `windeployqt.exe`，需要指定 Qt bin 目录下的工具路径：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe `
  -WindeployQtPath D:\Qt\6.x.x\msvcXXXX_64\bin\windeployqt.exe
```

生成 Inno Setup 安装器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe `
  -BuildInstaller
```

安装器最终默认输出到项目根目录：

```text
QtDesktopPet-<version>-windows-x64-setup.exe
```

脚本会先让 Inno Setup 输出到当前盘符下的纯 ASCII 临时目录，例如 `D:\QtDesktopPetInstallerOutput`，再把最终安装包复制回项目根目录，避免中文项目路径下出现 `EndUpdateResource failed (5)`。如果需要改变最终安装包目录，可传入 `-InstallerOutputDir`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe `
  -BuildInstaller `
  -InstallerOutputDir D:\ReleaseOutput
```

如果需要改变 Inno Setup 的临时编译输出目录，可传入 `-InstallerWorkOutputDir`。

本地生成的安装包也可以集中放到 `release_packages/`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package_release.ps1 `
  -ExePath build/release/QtDesktopPet.exe `
  -BuildInstaller `
  -InstallerOutputDir release_packages
```

`dist/` 和 `release_packages/` 都是本地发布产物目录，不进入 Git。

脚本默认优先查找：

```text
D:\Inno Setup 7\ISCC.exe
D:\Inno Setup 6\ISCC.exe
C:\Program Files (x86)\Inno Setup 7\ISCC.exe
C:\Program Files (x86)\Inno Setup 6\ISCC.exe
```

如果 Inno Setup 安装在其他位置，可传入 `-InnoCompilerPath`。

安装器卸载时会询问是否同时删除用户配置、导入角色、聊天记录和日志；用户确认后会在卸载完成阶段删除当前用户下的 QtDesktopPet 数据目录。

## 开发诊断

仓库提供开发用性能采样脚本，不进入普通用户发布包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/perf_sample.ps1
```

默认采样当前 `QtDesktopPet` 进程 5 分钟，每 5 秒一条，CSV 输出到：

```text
reports/perf/
```

`reports/perf/` 已加入 `.gitignore`。稳定性检查记录模板见：

```text
docs/performance_stability_check.md
```

发布包应排除 `tools/`、`docs/`、`reports/`、`build/`、`dist/`、`release_packages/` 和 `.git/`，只保留运行必需文件、`resources/characters/`、`resources/icons/`、`LICENSE` 和必要说明。

## AI 配置和聊天

当前正式聊天支持 OpenAI Compatible 和 Google Gemini 两类协议。已提供以下 Provider 配置入口：

- OpenAI
- Google
- DeepSeek
- Custom

其中 OpenAI、DeepSeek、Custom 走 OpenAI Compatible 形式配置；Google 走 Gemini 原生 REST 接口。旧版保存过的已废弃 Provider 配置会在读取 AI 配置时清理，废弃 Provider 被选中时会回退为 `custom`。

已支持：

- 用户自定义 Base URL
- 用户自定义 API Key
- 用户自定义 Model
- 用户自定义 Path
- 超时、Temperature、Max Tokens
- 流式输出
- Google Gemini `generateContent` / `streamGenerateContent`
- 请求中切换 `think`
- 收到首段输出后切换并保持 `talk`
- 失败时切换 `error`
- API Key 不写入日志，不在错误提示中完整显示
- 对话历史面板记录用户消息和 AI 最终回复

AI 测试入口已从角色右键菜单移除，并迁移到设置页。

## 隐私说明

程序只会把用户消息发送到用户自己配置的接口。用户需要自行判断第三方代理、中转服务或自建服务是否可信。项目不会默认承诺第三方接口的隐私安全。

日志会记录请求诊断信息，例如 Provider、Base URL 主机、Path、HTTP 状态码、响应大小、错误摘要等；日志不应记录完整 API Key、Authorization Header、完整消息正文或完整错误响应正文。错误响应只保留脱敏摘要。

当前对话历史默认保存在内存中，已支持内存历史上限、请求上下文截取和可选本地历史保存；相关上限可在设置页调整。

## 素材版权说明

源码采用 MIT License。

角色素材、图片、图标等资源需要单独确认授权。当前 `shiroko` 素材用于桌宠加载器、动画和状态切换测试；在公开发布或正式分发前，需要确认素材的版权、再分发权限和适用范围。

用户导入或替换的素材，其版权责任由用户自行承担。

## 许可证

项目源码使用 MIT License，见 [LICENSE](LICENSE)。
