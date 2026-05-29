# QtDesktopPet

QtDesktopPet 是一个基于 Qt Widgets / C++17 的 Windows 桌面宠物原型项目。当前目标是完成一个轻量桌宠内核，并在此基础上接入用户自定义的大模型对话能力。

## 当前状态

已实现：

- 透明无边框桌宠窗口
- 鼠标拖动
- 右键菜单退出和状态测试
- 置顶切换
- `shiroko` 角色包读取
- PNG 序列帧动画播放
- `idle` / `talk` / `think` / `sleep` / `happy` / `drag` / `error` 状态
- 托盘显示、隐藏、退出
- 隐藏时暂停动画，显示时恢复动画
- 保存窗口位置、置顶、缩放和性能设置
- 文件日志和基础轮转
- 设置窗口
- 应用设置：缩放、性能模式、隐藏暂停、懒加载
- AI Provider 分组配置
- Windows DPAPI 加密保存 API Key
- 非 Windows 环境经用户确认后明文保存 API Key
- OpenAI Compatible 聊天请求
- SSE 流式输出
- 聊天输入框
- AI 回复气泡
- 对话历史面板
- AI 请求取消和对话清空
- Google Gemini 原生聊天请求

尚未实现：

- 设置页内 AI 连通性测试
- 角色导入/切换界面
- 对话历史持久化
- 打包发布脚本

## 技术栈

- C++17
- Qt 6 Widgets
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

## 角色包

当前默认角色包位于：

```text
shiroko/
```

基本结构：

```text
shiroko/
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

当前素材版本为 `2.1.0-stable`，所有帧使用 512x512 透明画布。当前实现会加载当前角色包的各状态帧并按 `character.json` 中的 FPS 播放。

后续计划将默认角色包位置整理为：

```text
resources/characters/shiroko/
```

## 配置和日志

应用配置保存到 Qt 标准配置目录：

```text
QStandardPaths::AppConfigLocation/app_config.json
```

配置损坏时会备份为：

```text
app_config.broken.json
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

AI 测试入口已从角色右键菜单移除，后续会放入设置页。

## 隐私说明

程序只会把用户消息发送到用户自己配置的接口。用户需要自行判断第三方代理、中转服务或自建服务是否可信。项目不会默认承诺第三方接口的隐私安全。

日志会记录请求诊断信息，例如 Provider、Base URL 主机、Path、HTTP 状态码、响应大小、错误摘要等；日志不应记录完整 API Key、Authorization Header 或完整消息正文。

当前对话历史保存在内存中，请求上下文只截取最近部分历史。后续仍需要补充内存历史上限和可选持久化策略。

## 素材版权说明

源码采用 MIT License。

角色素材、图片、图标等资源需要单独确认授权。当前 `shiroko` 素材用于桌宠加载器、动画和状态切换测试；在公开发布或正式分发前，需要确认素材的版权、再分发权限和适用范围。

用户导入或替换的素材，其版权责任由用户自行承担。

## 许可证

项目源码使用 MIT License，见 [LICENSE](LICENSE)。
