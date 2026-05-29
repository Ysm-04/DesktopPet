# Qt 桌宠项目开发文档

## 1. 项目目标

本项目计划从零创建一个基于 **Qt / C++** 的 Windows 桌面宠物程序。

核心目标：

```text
1. 创建一个透明、无边框、可拖动、可置顶的桌宠窗口
2. 桌宠形象采用 PNG 图片序列实现动画
3. 每个角色拥有多个状态，每个状态拥有独立帧动画
4. 支持用户导入自定义角色资源包
5. 支持接入大模型 API 进行文字对话
6. AI 请求、回复、失败等过程需要和桌宠动画状态联动
7. 提供宽泛的 AI 接口配置入口，支持用户自己的代理、中转、自建接口
8. 程序需要轻量、低资源占用，保证轻薄本也能流畅运行
9. 项目开源，推荐使用 MIT License 或其他符合项目实际需求的开源许可
```

当前版本目标不是做完整商业产品，而是先实现一个：

```text
基于 Qt 的轻量级多状态 PNG 帧动画桌宠 + 可自定义 AI 接口对话助手
```

---

## 2. 第一版范围

### 2.1 第一版需要实现

```text
1. Qt Widgets + CMake 项目
2. 透明无边框桌宠窗口
3. 鼠标拖动
4. 置顶开关
5. 右键菜单
6. 系统托盘
7. 默认角色包加载
8. PNG 图片序列帧动画
9. 多状态动画切换
10. 基础状态机
11. AI 对话请求
12. OpenAI Compatible 自定义接口配置
13. AI 回复气泡
14. 本地配置保存
15. 基础日志
16. 基础错误兜底
17. 低资源占用策略
```

### 2.2 第一版暂不实现

```text
1. 语音输入
2. 语音朗读
3. 视频形象
4. GIF 形象
5. Live2D
6. 骨骼动画
7. AI 自动生成角色素材
8. 角色包市场
9. 插件系统
10. 云端同步
11. 自动更新
12. 长期记忆系统
13. 完整安装器
```

第一版要控制范围，不要因为扩展功能过多导致项目失控。

---

## 3. 平台目标

第一版明确优先支持：

```text
Windows 10 / Windows 11
```

要求：

```text
1. 以 Windows 桌面体验为主要目标
2. 透明窗口、置顶、系统托盘、拖动行为优先保证 Windows 正常
3. 代码结构尽量保持清晰，避免不必要的平台绑定
4. 第一版不承诺完整适配 macOS / Linux
```

后续如果需要跨平台，再单独评估不同系统下的透明窗口、托盘、窗口置顶、多屏和权限行为。

---

## 4. 开源许可

项目计划开源。

推荐许可：

```text
MIT License
```

注意事项：

```text
1. 如果采用 MIT License，需要在仓库中提供 LICENSE 文件
2. 项目源码可以使用 MIT License
3. 预设角色素材必须自制，或确认拥有可发布、可再分发、可商用或符合项目要求的授权
4. 第三方库、图标、字体、角色素材必须分别确认许可
5. 用户导入的素材版权由用户自行负责
6. 不要把许可不清晰的素材放入仓库
7. 使用 Qt 时，需要遵守所使用 Qt 版本对应的许可要求
```

如果未来要发布闭源商业版本，需要重新评估 Qt 许可、第三方依赖许可和素材授权。

---

## 5. 技术选型

请从零创建一个新的 Qt 项目。

建议技术路线：

```text
语言：C++17 或更高
框架：Qt 6
界面：Qt Widgets
构建：CMake
配置文件：JSON
角色素材：PNG 图片序列
网络请求：QNetworkAccessManager
动画驱动：QTimer
目标平台：Windows 10 / Windows 11
```

选择 Qt Widgets 的原因：

```text
1. MVP 开发速度快
2. 透明无边框窗口实现直接
3. 桌宠主体 UI 不复杂
4. 便于控制资源占用
5. C++ 逻辑组织清晰
```

---

## 6. 项目目录建议

建议创建如下目录结构：

```text
DesktopPet/
 ├── CMakeLists.txt
 ├── LICENSE
 ├── README.md
 ├── main.cpp
 ├── src/
 │   ├── app/
 │   │   ├── AppController.h
 │   │   └── AppController.cpp
 │   ├── ui/
 │   │   ├── PetWindow.h
 │   │   ├── PetWindow.cpp
 │   │   ├── PetView.h
 │   │   ├── PetView.cpp
 │   │   ├── ChatBubble.h
 │   │   ├── ChatBubble.cpp
 │   │   ├── SettingsDialog.h
 │   │   └── SettingsDialog.cpp
 │   ├── character/
 │   │   ├── CharacterPackage.h
 │   │   ├── CharacterPackage.cpp
 │   │   ├── CharacterPackageLoader.h
 │   │   ├── CharacterPackageLoader.cpp
 │   │   ├── AnimationClip.h
 │   │   ├── AnimationClip.cpp
 │   │   ├── FrameAnimator.h
 │   │   └── FrameAnimator.cpp
 │   ├── state/
 │   │   ├── PetStateMachine.h
 │   │   └── PetStateMachine.cpp
 │   ├── ai/
 │   │   ├── LLMProvider.h
 │   │   ├── LLMProvider.cpp
 │   │   ├── OpenAICompatibleProvider.h
 │   │   ├── OpenAICompatibleProvider.cpp
 │   │   ├── ConversationManager.h
 │   │   └── ConversationManager.cpp
 │   ├── config/
 │   │   ├── ConfigManager.h
 │   │   └── ConfigManager.cpp
 │   ├── tray/
 │   │   ├── TrayController.h
 │   │   └── TrayController.cpp
 │   └── util/
 │       ├── Logger.h
 │       └── Logger.cpp
 ├── resources/
 │   ├── icons/
 │   └── characters/
 │       └── default_pet/
 │           ├── character.json
 │           ├── preview.png
 │           ├── idle/
 │           ├── talk/
 │           ├── think/
 │           ├── sleep/
 │           └── error/
 └── docs/
     └── character_package_format.md
```

---

## 7. 核心模块职责

### 7.1 AppController

负责整体应用启动与模块连接。

职责：

```text
1. 初始化应用配置
2. 初始化桌宠窗口
3. 初始化系统托盘
4. 加载默认角色
5. 初始化 AI Provider
6. 连接状态机、动画、AI 请求之间的信号
7. 管理应用退出前的配置保存
```

---

### 7.2 PetWindow

桌宠主窗口。

职责：

```text
1. 创建透明无边框窗口
2. 承载 PetView 和 ChatBubble
3. 支持鼠标拖动
4. 支持右键菜单
5. 支持置顶
6. 支持缩放
7. 支持锁定位置
8. 保存和恢复窗口位置
9. 避免窗口启动后出现在屏幕外
```

注意：

```text
1. 不要使用普通主窗口边框
2. 不要在窗口绘制里做耗时逻辑
3. 不要在 paintEvent 中加载图片
4. 鼠标拖动时切换到 drag 状态
```

---

### 7.3 PetView

负责显示当前动画帧。

职责：

```text
1. 接收 FrameAnimator 当前帧
2. 绘制 PNG 图像
3. 保持透明背景
4. 根据缩放比例绘制角色
5. 避免无意义重绘
```

---

### 7.4 ChatBubble

负责显示桌宠气泡。

职责：

```text
1. 显示用户输入或 AI 回复
2. 支持自动换行
3. 支持超长文本裁剪
4. 支持定时隐藏
5. 位置根据 character.json 的 bubble.offsetX / offsetY 计算
```

建议限制：

```text
最大宽度：300px ~ 420px
最大显示行数：6 ~ 8 行
超出部分省略，后续可通过聊天窗口查看完整内容
```

---

### 7.5 CharacterPackage

表示一个角色资源包。

一个角色包包含：

```text
1. 角色 ID
2. 显示名称
3. 作者
4. 版本
5. 预览图
6. 默认状态
7. 基础尺寸
8. 缩放比例
9. 锚点
10. 气泡偏移
11. 多个动画状态
```

---

### 7.6 CharacterPackageLoader

负责加载角色包。

职责：

```text
1. 读取 character.json
2. 校验 JSON 格式
3. 校验 schemaVersion
4. 校验状态目录
5. 校验 PNG 帧文件
6. 构建 CharacterPackage
7. 支持缺失状态降级
8. 角色加载失败时回退默认角色
```

不能因为资源包不完整导致程序崩溃。

---

### 7.7 AnimationClip

表示一个动画状态。

例如：

```text
idle
talk
think
sleep
happy
drag
error
```

每个 AnimationClip 包含：

```text
1. 状态名
2. 帧路径列表
3. FPS
4. 是否循环
5. 播放结束后的 next 状态
6. 图片缓存
7. 是否已加载
```

---

### 7.8 FrameAnimator

帧动画播放器。

职责：

```text
1. 按照当前 AnimationClip 的 FPS 播放帧
2. 使用 QTimer 驱动动画
3. 切换状态时重置帧索引
4. loop 为 true 时循环播放
5. loop 为 false 时播放完切换 next 状态
6. 发出 frameChanged 信号给 PetView
7. 窗口隐藏后暂停动画
```

禁止：

```text
1. 每帧从硬盘读取图片
2. 使用 while 循环驱动动画
3. 使用 1ms / 5ms 这种高频定时器
4. 隐藏到托盘后继续高频播放动画
```

---

### 7.9 PetStateMachine

桌宠状态机。

职责：

```text
1. 统一管理状态切换
2. 管理状态优先级
3. 响应用户拖动、点击、AI 请求、AI 回复、错误等事件
4. 避免状态切换逻辑散落在多个类中
```

第一版状态：

```text
idle    待机
talk    说话
think   思考
sleep   睡觉
happy   开心
drag    被拖动
error   出错
```

建议优先级：

```text
drag > error > think > talk > happy > sleep > idle
```

基本规则：

```text
程序启动               → idle
用户拖动桌宠           → drag
用户松开鼠标           → idle
等待 AI 返回           → think
AI 正在输出回复        → talk
AI 回复完成            → idle
请求失败               → error
长时间无操作           → sleep
点击桌宠               → happy
```

---

### 7.10 TrayController

负责系统托盘。

职责：

```text
1. 创建托盘图标
2. 创建托盘菜单
3. 显示 / 隐藏桌宠
4. 切换置顶
5. 打开设置
6. 退出程序
```

---

### 7.11 ConfigManager

负责配置读写。

职责：

```text
1. 读取应用配置
2. 保存应用配置
3. 读取 AI 配置
4. 保存 AI 配置
5. 处理配置损坏
6. 根据便携模式决定配置目录
```

配置文件损坏时，不要直接覆盖，先备份为：

```text
app_config.broken.json
ai_config.broken.json
```

然后再生成默认配置。

---

### 7.12 Logger

负责日志。

记录：

```text
1. 程序启动
2. 角色包加载
3. 状态切换
4. 图片加载失败
5. AI 请求开始
6. AI 请求结束
7. HTTP 状态码
8. JSON 解析失败
9. 配置保存失败
10. 资源释放
```

禁止：

```text
1. 输出完整 API Key
2. 日志文件无限增长
3. 高频动画每帧写日志
```

建议：

```text
单日志文件最大 2MB
保留最近 3 个日志文件
```

---

## 8. 角色资源包格式

本项目第一版不使用视频和 GIF。

统一使用：

```text
PNG 图片序列 + character.json
```

核心结构：

```text
角色 Character
 └── 状态 State
      └── 帧序列 Frames
```

也就是说：

```text
一个角色不是一组图片
一个角色是一个资源包
资源包里有多个状态
每个状态有自己的一组连续 PNG 图片
```

---

### 8.1 角色包目录结构

示例：

```text
characters/
 └── default_pet/
     ├── character.json
     ├── preview.png
     ├── idle/
     │   ├── 000.png
     │   ├── 001.png
     │   ├── 002.png
     │   └── 003.png
     ├── talk/
     │   ├── 000.png
     │   ├── 001.png
     │   └── 002.png
     ├── think/
     │   ├── 000.png
     │   ├── 001.png
     │   └── 002.png
     ├── sleep/
     │   ├── 000.png
     │   ├── 001.png
     │   └── 002.png
     └── error/
         ├── 000.png
         ├── 001.png
         └── 002.png
```

---

### 8.2 character.json 示例

```json
{
  "schemaVersion": 1,
  "id": "default_pet",
  "displayName": "默认桌宠",
  "author": "DesktopPet",
  "version": "1.0.0",

  "preview": "preview.png",
  "defaultState": "idle",

  "base": {
    "width": 256,
    "height": 256,
    "scale": 1.0,
    "anchorX": 0.5,
    "anchorY": 1.0
  },

  "bubble": {
    "offsetX": 0,
    "offsetY": -180
  },

  "states": {
    "idle": {
      "path": "idle",
      "fps": 8,
      "loop": true
    },
    "talk": {
      "path": "talk",
      "fps": 12,
      "loop": true
    },
    "think": {
      "path": "think",
      "fps": 6,
      "loop": true
    },
    "sleep": {
      "path": "sleep",
      "fps": 4,
      "loop": true
    },
    "error": {
      "path": "error",
      "fps": 6,
      "loop": false,
      "next": "idle"
    }
  }
}
```

---

### 8.3 角色包版本兼容策略

角色包使用 `schemaVersion` 控制格式版本。

规则：

```text
1. 当前程序只保证兼容已支持的 schemaVersion
2. 遇到更高版本角色包时，不要强行加载
3. 遇到更高版本角色包时，提示用户“角色包版本过高，请升级程序”
4. 后续如果格式升级，应提供迁移函数
5. 不要静默忽略关键字段变化
```

---

### 8.4 角色包校验规则

加载角色包时必须校验：

```text
1. character.json 是否存在
2. schemaVersion 是否支持
3. id 是否为空
4. defaultState 是否存在
5. states 是否为空
6. 每个 state 的 path 是否存在
7. 每个 state 是否至少有一帧 PNG
8. fps 是否大于 0
9. loop 为 false 时，next 状态是否存在
10. 图片尺寸是否合理
```

允许角色包只提供部分状态。

最小角色包可以只有：

```text
idle
talk
```

缺失状态降级规则：

```text
缺少 talk  → 使用 idle
缺少 think → 使用 idle
缺少 sleep → 使用 idle
缺少 error → 使用 idle
缺少 drag  → 使用 idle
缺少 happy → 使用 idle
```

---

### 8.5 默认角色兜底

程序必须内置一个最小默认角色。

要求：

```text
1. 即使用户角色包全部损坏，程序也必须能启动
2. 默认角色至少包含 idle 状态
3. 默认角色素材必须自制或授权清晰
4. 用户当前角色加载失败时，自动回退默认角色
```

---

## 9. AI 接入设计

AI 接入不要写死某一家平台。

目标是支持：

```text
1. OpenAI Compatible API
2. 用户自定义 Base URL
3. 用户自定义 API Key
4. 用户自定义模型名
5. 用户自建代理
6. 用户中转接口
7. 本地网关
8. 聚合接口
```

第一版重点支持：

```text
OpenAI Compatible
```

但配置入口要足够宽泛。

---

### 9.1 AI 配置项

普通配置：

```text
服务类型：OpenAI Compatible
Base URL
API Key
Model
测试连接
保存配置
```

高级配置：

```text
请求路径
是否启用流式输出
Temperature
Max Tokens
超时时间
自定义 Header
```

第一版可以先实现非流式，流式接口预留。

---

### 9.2 ai_config.json 示例

```json
{
  "providerType": "openai-compatible",
  "name": "自定义代理",
  "baseUrl": "https://api.example.com/v1",
  "apiKey": "sk-xxxx",
  "model": "deepseek-chat",
  "path": "/chat/completions",
  "stream": false,
  "timeoutMs": 60000,
  "temperature": 0.7,
  "maxTokens": 1024,
  "headers": {}
}
```

实际请求地址：

```text
POST {baseUrl}{path}
```

示例：

```text
https://api.example.com/v1/chat/completions
```

---

### 9.3 LLMProvider 抽象

设计接口：

```text
LLMProvider
 ├── OpenAICompatibleProvider
 └── CustomHttpProvider
```

第一版只实现：

```text
OpenAICompatibleProvider
```

但保留 CustomHttpProvider 的接口位置。

后续 CustomHttpProvider 可支持：

```text
1. 自定义 URL
2. 自定义 Method
3. 自定义 Headers
4. 自定义 Body Template
5. 自定义 Response Path
6. 自定义 Stream Delta Path
```

---

### 9.4 AI 请求与桌宠状态联动

正常流程：

```text
用户输入消息
↓
切换 think 状态
↓
发送 API 请求
↓
收到回复
↓
切换 talk 状态
↓
气泡显示回复
↓
回复显示完成
↓
切换 idle 状态
```

失败流程：

```text
用户输入消息
↓
切换 think 状态
↓
请求失败 / 超时 / JSON 解析失败
↓
切换 error 状态
↓
气泡显示错误提示
↓
回到 idle
```

---

### 9.5 AI 并发策略

第一版明确：

```text
1. 同一时间只允许一个 AI 请求
2. AI 回复期间，用户再次发送消息时提示“正在回复中”
3. 不允许请求队列无限堆积
4. 后续可支持“取消当前请求并重新发送”
5. 请求失败、取消或完成后，必须正确恢复状态机
```

---

### 9.6 AI 错误处理

必须处理：

```text
1. Base URL 为空
2. API Key 为空
3. Model 为空
4. HTTP 401
5. HTTP 404
6. HTTP 429
7. HTTP 500
8. 请求超时
9. 返回不是 JSON
10. JSON 格式不符合 OpenAI Compatible 格式
11. 网络断开
12. 用户取消请求
```

错误提示分两层：

```text
用户提示：简洁易懂
日志信息：保留具体调试信息
```

不要在日志中输出完整 API Key。

只允许输出类似：

```text
sk-****abcd
```

---

## 10. 对话系统

### 10.1 ConversationManager

职责：

```text
1. 保存当前会话消息
2. 构建请求 messages
3. 限制上下文长度
4. 清空会话
5. 后续支持人格设定
```

第一版上下文限制：

```text
最多保留最近 10 轮对话
超过后丢弃更早内容
```

不要无限增长。

---

### 10.2 人格设定预留

第一版可以先使用简单默认人格。

后续可扩展：

```json
{
  "name": "小星",
  "personality": "温柔、简洁、有一点吐槽感",
  "systemPrompt": "你是用户桌面上的虚拟助手。"
}
```

---

## 11. 性能与资源占用要求

本项目是常驻桌面的后台应用，必须控制资源占用。

基本原则：

```text
1. 空闲时 CPU 占用应尽量接近 0
2. 不允许无意义高频刷新
3. 不允许每帧从硬盘读取图片
4. 不允许一次性加载所有角色资源
5. 不允许 AI 请求阻塞 UI 线程
6. 不允许聊天历史无限增长
7. 不允许日志无限写入
8. 不允许窗口隐藏后仍然高频播放动画
```

---

### 11.1 帧率要求

默认帧率建议：

```text
idle   6~8 FPS
talk   10~12 FPS
think  6~8 FPS
sleep  2~4 FPS
happy  8~12 FPS
error  6~8 FPS
drag   8~12 FPS
```

不需要 30 FPS 或 60 FPS。

桌宠不是游戏角色，不应高频刷新。

---

### 11.2 图片资源限制

建议限制：

```text
单帧推荐尺寸：256x256 或 384x384
单帧最大建议：512x512
单状态帧数建议：不超过 60 帧
单角色包建议：不超过 50MB
```

超过建议值时不一定禁止，但需要警告用户：

```text
该角色包较大，可能增加内存占用或导致低配设备卡顿。
```

---

### 11.3 图片加载策略

不要启动时加载所有角色。

建议：

```text
1. 启动时只加载当前角色
2. 当前角色优先加载 idle / talk / think / sleep
3. 其他状态首次使用时延迟加载
4. 切换角色时释放旧角色资源
5. 对超大图片进行警告
6. 必要时生成缩放后的缓存图
```

注意：

```text
1. 不要重复缓存同一张图片
2. 不要在 paintEvent 中加载图片
3. 不要每次状态切换都重新扫描目录
4. 不要每次绘制都解析 character.json
```

---

### 11.4 内存目标

第一版建议目标：

```text
空闲状态内存占用：尽量控制在 100MB 以内
普通角色包运行：尽量控制在 150MB 以内
AI 对话过程中：内存不应持续增长
切换角色后：旧角色资源应正确释放
```

重点检查：

```text
1. QPixmap / QImage 是否重复缓存
2. 旧角色资源是否释放
3. 对话历史是否无限保存
4. 日志文件是否无限增大
5. QNetworkReply 是否释放
6. QTimer 是否重复创建但未停止
```

---

### 11.5 CPU 占用要求

禁止：

```text
1. while 循环驱动动画
2. 高频 QTimer
3. 不可见时仍然播放动画
4. 每帧扫描目录
5. 每帧解析 JSON
6. 每帧加载图片
```

动画计时：

```text
frameInterval = 1000 / fps
```

例如：

```text
8 FPS  → 125ms
12 FPS → 83ms
4 FPS  → 250ms
```

---

### 11.6 窗口隐藏后的行为

当桌宠隐藏到托盘后：

```text
1. 暂停动画
2. 停止不必要定时器
3. 不刷新 PetView
4. 不显示气泡
5. 不执行无意义后台任务
```

重新显示后再恢复动画。

---

### 11.7 低功耗模式

第一版建议预留设置项：

```text
性能模式：
1. 标准模式
2. 低功耗模式
```

低功耗模式下：

```text
1. idle FPS 降低
2. sleep 更容易触发
3. 窗口不可见时暂停动画
4. 气泡隐藏后减少刷新
5. 禁止后台无意义定时任务
```

---

## 12. 用户数据目录与便携模式

正式桌面程序不应默认把用户配置写到程序安装目录。

建议：

```text
1. 开发阶段可以使用项目目录下的 config/ 和 resources/
2. 正式版本应使用 Qt 标准应用数据目录
3. 配置文件、用户角色包、日志文件应保存到用户可写目录
```

建议目录职责：

```text
AppConfigLocation：保存 app_config.json、ai_config.json
AppDataLocation：保存用户导入角色包、会话历史等数据
CacheLocation：保存缩放缓存、临时缓存
```

---

### 12.1 便携模式

支持 portable 模式。

规则：

```text
1. 如果程序目录下存在 portable.flag，则启用便携模式
2. 便携模式下，配置、角色包、日志保存在程序目录
3. 非便携模式下，使用系统用户数据目录
4. 便携模式下也要处理写权限失败
```

---

## 13. 配置文件设计

建议配置分开存储：

```text
app_config.json
ai_config.json
conversation_history.json
```

### 13.1 app_config.json

```json
{
  "window": {
    "x": 100,
    "y": 100,
    "scale": 1.0,
    "alwaysOnTop": true,
    "locked": false
  },
  "character": {
    "current": "default_pet"
  },
  "performance": {
    "mode": "standard",
    "pauseWhenHidden": true,
    "enableLazyLoad": true
  }
}
```

---

### 13.2 API Key 存储

第一版可以先保存在本地配置中，但必须注意：

```text
1. 不要上传到任何服务器
2. 不要写入日志
3. 不要在错误信息中完整展示
4. UI 中默认隐藏
5. README 中说明 API Key 仅保存在本地
```

界面提示：

```text
API Key 仅保存在本地配置文件中，不会上传到作者服务器。
```

后续可考虑系统凭据管理：

```text
Windows Credential Manager
macOS Keychain
Linux Secret Service
```

第一版目标平台是 Windows，因此后续优先考虑 Windows Credential Manager。

---

## 14. 窗口与桌面体验

桌宠窗口需要支持：

```text
1. 透明背景
2. 无边框
3. 鼠标拖动
4. 右键菜单
5. 双击打开聊天
6. 置顶切换
7. 锁定位置
8. 缩放
9. 隐藏到托盘
10. 退出程序
```

右键菜单建议：

```text
开始对话
切换角色
切换置顶
锁定位置
性能模式
打开设置
重新加载角色
隐藏
退出
```

---

### 14.1 透明区域点击策略

第一版采用简单策略：

```text
1. 按窗口矩形区域处理点击
2. 暂不做基于 PNG alpha 的透明区域点击穿透
3. 后续可扩展透明像素命中检测
```

不要第一版就实现复杂点击穿透，避免拖慢 MVP。

---

## 15. 多屏幕与 DPI

必须考虑：

```text
1. 高 DPI 缩放
2. 多显示器
3. 副屏负坐标
4. 主屏幕变化
5. 分辨率变化
6. 启动时窗口位置不可见
```

如果保存的位置不在任何屏幕范围内，启动时自动移动到主屏幕可见区域。

---

## 16. 错误处理与兜底

程序不能因为以下情况崩溃：

```text
1. character.json 不存在
2. character.json 格式错误
3. 状态目录不存在
4. 状态帧为空
5. PNG 加载失败
6. 帧尺寸不一致
7. Base URL 为空
8. API Key 为空
9. Model 为空
10. 网络请求失败
11. 请求超时
12. 返回 JSON 不符合预期
13. 角色包过大
14. 配置文件损坏
15. 用户数据目录不可写
```

必须有降级策略：

```text
角色加载失败 → 加载默认角色
状态缺失     → 回退 idle
AI 请求失败  → 显示错误气泡并切 error
配置损坏     → 备份损坏文件并生成默认配置
目录不可写   → 提示用户并尝试使用备用目录
```

---

## 17. 隐私、版权与风险提示

### 17.1 用户素材版权

要求：

```text
1. 预设素材必须自制或授权清晰
2. 用户导入素材的版权责任由用户自行承担
3. 后续如果支持角色包分享，必须加入版权提示
4. 不要在仓库中放入来源不明的动漫、游戏、影视、真人照片素材
```

---

### 17.2 AI 接口隐私

要求：

```text
1. 程序只负责把用户消息发送到用户配置的接口
2. 用户应自行确认第三方代理、中转服务是否可信
3. 不要默认承诺第三方代理的隐私安全
4. README 中需要说明自定义代理可能带来的隐私风险
5. API Key 和聊天内容不得发送到作者服务器
```

---

## 18. 打包部署要求

Windows 下不能只拷贝 exe。

需要检查：

```text
1. Qt DLL 是否完整
2. platforms 插件是否完整
3. imageformats 插件是否完整
4. PNG 是否可以正常加载
5. 托盘图标是否正常
6. 无 Qt 开发环境的电脑上是否可以运行
7. 配置目录是否可写
8. 便携模式是否可用
```

建议后续提供：

```text
1. Windows x64 Release 构建
2. 打包脚本
3. README 部署说明
4. 示例角色包
```

第一版可以先不做安装器，但需要保证构建产物可以在普通 Windows 环境运行。

---

## 19. 编码规范

使用 C++。

大括号使用 Allman 风格：

```cpp
void Example::run()
{
    if (condition)
    {
        doSomething();
    }
}
```

要求：

```text
1. 类职责清晰
2. 不要把所有逻辑写进 PetWindow
3. 不要写超长函数
4. 不要硬编码路径
5. 不要硬编码单一 AI 平台
6. 不要在 UI 线程执行耗时任务
7. 不要在 paintEvent 里做资源加载
8. 不要输出完整 API Key
9. 不要无意义高频刷新
```

---

## 20. 第一阶段开发顺序

### 阶段 1：创建 Qt 项目与基础窗口

```text
1. 创建新的 Qt Widgets + CMake 项目
2. 创建 PetWindow
3. 实现透明无边框窗口
4. 显示默认 PNG 图片
5. 支持鼠标拖动
6. 支持右键退出
7. 支持置顶开关
```

---

### 阶段 2：系统托盘

```text
1. 创建 TrayController
2. 显示托盘图标
3. 添加右键菜单
4. 支持显示 / 隐藏桌宠
5. 支持退出程序
```

---

### 阶段 3：角色包加载

```text
1. 创建 CharacterPackage
2. 创建 CharacterPackageLoader
3. 读取 resources/characters/default_pet/character.json
4. 解析 states
5. 加载 idle 状态帧
6. 校验资源包
7. 缺失状态回退 idle
8. 角色包损坏时回退默认角色
```

---

### 阶段 4：帧动画播放器

```text
1. 创建 AnimationClip
2. 创建 FrameAnimator
3. 使用 QTimer 按 FPS 播放帧
4. 支持 loop
5. 支持 next
6. 支持切换状态
7. 窗口隐藏时暂停动画
```

---

### 阶段 5：状态机

```text
1. 创建 PetStateMachine
2. 支持 idle / talk / think / sleep / drag / error
3. 拖动时切 drag
4. 松开后切 idle
5. 错误时切 error
6. 非循环状态播放完回 idle
```

---

### 阶段 6：AI 接入

```text
1. 创建 LLMProvider
2. 创建 OpenAICompatibleProvider
3. 支持 Base URL / API Key / Model / Path
4. 使用 QNetworkAccessManager 异步请求
5. 支持超时
6. 支持取消请求接口预留
7. 普通非流式回复显示到 ChatBubble
8. 请求中切 think
9. 收到回复后切 talk
10. 结束后切 idle
11. 失败后切 error
12. 同时只允许一个请求
```

---

### 阶段 7：配置界面

```text
1. 创建 SettingsDialog
2. 配置 Base URL
3. 配置 API Key
4. 配置 Model
5. 配置请求路径
6. 测试连接
7. 选择角色
8. 设置置顶
9. 设置性能模式
10. 设置便携模式提示
```

---

### 阶段 8：性能与稳定性检查

```text
1. 静置测试 CPU 和内存
2. 检查隐藏到托盘后动画是否暂停
3. 检查角色切换后资源释放
4. 检查连续对话后内存是否持续增长
5. 检查损坏配置文件是否能恢复
6. 检查损坏角色包是否能回退默认角色
```

---

## 21. 验收标准

第一版完成后，应满足：

```text
1. 程序可以启动
2. 桌宠窗口透明无边框
3. 桌宠可以拖动
4. 桌宠可以置顶
5. 右键菜单可用
6. 系统托盘可用
7. 可以加载默认角色包
8. idle 动画可以正常播放
9. 可以切换 think / talk / error 状态
10. 可以填写自定义 AI Base URL / API Key / Model
11. 可以发送一条消息并显示回复
12. AI 请求过程中桌宠状态会变化
13. 请求失败不会崩溃
14. 隐藏到托盘后动画暂停
15. 空闲状态 CPU 占用较低
16. 内存不会随时间持续上涨
17. 配置损坏后可以备份并恢复默认配置
18. 用户角色损坏后可以回退默认角色
19. API Key 不会完整输出到日志
20. Windows 10/11 下基本功能可用
```

---

## 22. 性能测试建议

至少测试：

```text
1. 启动后静置 10 分钟，观察 CPU 和内存
2. idle 动画连续播放 10 分钟
3. 连续发送 20 轮 AI 对话
4. 连续切换角色 10 次
5. 隐藏到托盘后观察 CPU 是否下降
6. 导入较大角色包，确认不会卡死
7. 在轻薄本或低压 CPU 设备上测试拖动流畅度
8. 修改配置文件为错误格式，确认程序不会崩溃
9. 删除当前角色包，确认可以回退默认角色
10. 使用错误 API Key，确认错误提示正常
```

---

## 23. 必须避免的问题

```text
1. 不要支持视频
2. 不要支持 GIF
3. 不要实现语音功能
4. 不要实现 Live2D
5. 不要写死 OpenAI 官方接口
6. 不要把所有逻辑写进一个类
7. 不要 UI 线程阻塞等待网络请求
8. 不要每一帧读取硬盘图片
9. 不要一次性加载所有角色包
10. 不要隐藏到托盘后继续高频刷新
11. 不要无限保存聊天历史
12. 不要无限写日志
13. 不要输出完整 API Key
14. 不要让错误配置导致程序崩溃
15. 不要忽略轻薄本性能表现
16. 不要把来源不明的素材放入仓库
17. 不要默认承诺第三方代理隐私安全
18. 不要第一版就实现复杂透明点击穿透
19. 不要为了跨平台过早引入复杂兼容层
20. 不要把配置默认写入没有权限的程序安装目录
```

---

## 24. README 最低要求

README 至少说明：

```text
1. 项目简介
2. 当前开发状态
3. Windows 优先支持
4. 构建方式
5. 角色包格式简介
6. AI 接口配置说明
7. API Key 本地保存说明
8. 第三方代理隐私风险提示
9. 素材版权提示
10. 开源许可
```

---

## 25. 当前版本总结

当前版本已经从桌宠内核推进到最小 AI 对话闭环。

核心是：

```text
Qt 透明桌宠窗口
Windows 优先
PNG 多状态帧动画角色包
轻量状态机
OpenAI Compatible 自定义 AI 接口
Google Gemini 原生 AI 接口
AI 对话与桌宠动画联动
SSE 流式输出
聊天输入框、回复气泡和对话历史面板
按 Provider 分组保存 AI 配置
Windows DPAPI 加密保存 API Key
低资源占用
MIT License 开源
```

一句话目标：

```text
先创建一个可运行、可对话、可换角色、资源占用低、后续容易扩展的 Qt 桌宠项目。
```

当前仍需补齐：

```text
1. 设置页内 AI 连通性测试
2. 对话历史内存上限和可选持久化
3. character.json 中 base、anchor、bubble offset 的解析与应用
4. 角色包位置整理和角色切换
5. 发布前素材授权确认与打包验证
```
