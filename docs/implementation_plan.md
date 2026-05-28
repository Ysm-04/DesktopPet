# QtDesktopPet 实施计划

本文档用于把 `Qt_DesktopPet_开发文档.md` 中的总体目标拆成可执行阶段。

说明：

```text
1. Qt_DesktopPet_开发文档.md 是项目总纲，定义长期目标、模块职责和整体约束
2. 本文档是当前实际开发顺序，会在不违背总纲的前提下进一步拆小阶段
3. 如果两份文档的阶段编号不同，以本文档作为当前执行计划
```

总原则：

```text
1. 先让程序跑起来，再逐步完善架构和功能
2. AI 接入放到桌宠内核稳定之后
3. UI 优化作为项目收尾，不阻塞早期功能验证
4. 每个阶段结束后做最小验收，避免问题堆到后期
5. 修改、删除、移动文件前需要先说明风险并获得确认
```

---

## 1. 已确认约定

```text
项目名：QtDesktopPet
当前目录：Qt_DesktopPet
主要平台：Windows 10 / Windows 11
界面框架：Qt Widgets
构建系统：CMake
优先编译链：Qt 6.5.3 + MinGW 11.2.0 + Ninja
默认角色包：shiroko
AI 接入：放到后期阶段
UI 优化：所有核心功能调试正常后再做
远程仓库：https://git.emoera.com/Make/Qt_DesktopPet.git
```

---

## 2. 本机工具检查结果

当前已检查到：

```text
Git：2.51.0.windows.1
CMake：3.29.3
Qt：6.5.3
Ninja：1.12.0
MinGW g++：11.2.0
MinGW mingw32-make：4.2.1
```

相关路径：

```text
Qt MinGW Kit：D:/Qt/6.5.3/mingw_64
Qt MSVC Kit：D:/Qt/6.5.3/msvc2019_64
MinGW 工具链：D:/Qt/Tools/mingw1120_64/bin
CMake：D:/Qt/Tools/CMake_64/bin/cmake.exe
Ninja：D:/Qt/Tools/Ninja/ninja.exe
```

注意：

```text
1. 当前 PATH 中的 qmake 默认指向 msvc2019_64
2. g++ 未直接出现在 PATH 中
3. 使用 MinGW 构建时，应显式指定 Qt MinGW 路径和编译器路径
```

建议 CMake 配置方式：

```powershell
cmake -S . -B build/mingw-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.5.3/mingw_64 `
  -DCMAKE_C_COMPILER=D:/Qt/Tools/mingw1120_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1120_64/bin/g++.exe
```

---

## 3. 角色包约定

`shiroko` 目录作为当前默认角色包。

已检查到的结构：

```text
shiroko/
 ├── character.json
 ├── preview.png
 ├── README.md
 ├── idle/
 ├── talk/
 ├── think/
 ├── sleep/
 ├── happy/
 ├── drag/
 └── error/
```

当前素材版本：`2.1.0-stable`。

当前帧数：

```text
idle  ：30 帧
talk  ：20 帧
think ：30 帧
sleep ：30 帧
happy ：20 帧
drag  ：30 帧
error ：20 帧
```

需要注意：

```text
1. character.json 中 base.width/base.height 当前为 512x512
2. 当前实现会预加载当前角色包的全部状态帧，后续需要观察内存和低配设备表现
3. 如果项目公开发布或推送远程仓库，需要确认 shiroko 素材版权和再分发权限
4. 版权不明确前，不应把它作为正式开源发布素材承诺
```

---

## 4. 阶段 0：仓库与工程准备

目标：

```text
建立可回滚、可构建的工程基础，不写复杂业务逻辑。
```

已确定操作：

```text
1. 初始化 Git 仓库
2. 配置 origin 为 https://git.emoera.com/Make/Qt_DesktopPet.git
3. 新增实施计划文档
```

后续待做：

```text
1. 创建 .gitignore
2. 创建 CMakeLists.txt
3. 创建 main.cpp
4. 创建 src/、resources/ 等基础目录
5. 决定 shiroko 是否移动到 resources/characters/shiroko
6. 创建最小 README.md
7. 确认 LICENSE 是否采用 MIT
```

验收标准：

```text
1. Git 仓库存在
2. origin 配置正确
3. CMake 能完成配置
4. 空项目或最小窗口项目能编译
```

需要确认：

```text
1. 是否创建 .gitignore
2. 是否创建 MIT LICENSE
3. 是否将 shiroko 移动到 resources/characters/shiroko
4. 是否立即创建最小 Qt 工程
```

---

## 5. 阶段 1：最小可运行桌宠窗口

目标：

```text
先让程序启动并显示一个可交互的透明桌宠窗口。
```

只做：

```text
1. Qt Widgets + CMake 工程
2. main.cpp
3. PetWindow
4. 透明无边框窗口
5. 显示单张占位 PNG 或 shiroko preview.png
6. 鼠标拖动
7. 右键退出
8. 置顶开关
```

暂不做：

```text
1. 完整角色包加载
2. 多状态动画
3. 状态机
4. 托盘
5. 配置系统
6. 日志系统
7. AI 接入
8. UI 美化
```

验收标准：

```text
1. 程序能编译
2. 程序能启动
3. 窗口透明无边框
4. 图片能显示
5. 鼠标能拖动窗口
6. 右键菜单能退出
7. 置顶开关生效
```

---

## 6. 阶段 2A：角色包最小读取

目标：

```text
先把 shiroko 角色包作为结构化数据读进程序，暂不播放动画。
```

只做：

```text
1. CharacterPackage
2. CharacterPackageLoader
3. 读取 shiroko/character.json
4. 校验 schemaVersion
5. 校验 defaultState
6. 校验 states
7. 读取 idle 状态配置
8. 收集 idle 帧路径
9. PetWindow 显示 idle 第一帧
10. 加载失败时回退 preview.png 或内置占位图
```

暂不做：

```text
1. 多角色切换
2. 角色导入界面
3. AnimationClip
4. FrameAnimator
5. QTimer 帧动画
6. 完整状态机
7. AI 联动
```

验收标准：

```text
1. 能读取 shiroko/character.json
2. 能获取 idle 状态帧路径列表
3. 能显示 idle 第一帧
4. 不在 paintEvent 中加载图片
5. character.json 缺失或损坏时程序不崩溃
6. idle 状态缺失时程序不崩溃
```

---

## 7. 阶段 2B：idle 帧动画

目标：

```text
在角色包读取稳定后，把 idle 第一帧显示升级为按 FPS 播放 PNG 序列帧。
```

只做：

```text
1. AnimationClip
2. FrameAnimator
3. 使用 QTimer 按 idle fps 播放帧
4. 启动时预加载 idle 帧到内存
5. loop=true 时循环播放
6. 仍然只播放 idle 状态
```

暂不做：

```text
1. 多状态切换
2. 状态机
3. 托盘隐藏暂停
4. AI 联动
5. 角色切换
```

验收标准：

```text
1. idle 动画正常播放
2. fps 使用 character.json 配置
3. 不每帧读取硬盘
4. 不在 paintEvent 中加载图片
5. QTimer 间隔不低于 character.json 推导值
6. 图片加载失败时跳过坏帧或回退 preview.png
```

---

## 8. 阶段 3：状态机与多状态动画

目标：

```text
集中管理桌宠状态，避免状态切换逻辑散落在窗口和动画类中。
```

只做：

```text
1. PetStateMachine
2. idle / drag / think / talk / happy / sleep / error
3. 拖动时切换 drag
4. 松开后回 idle
5. 右键菜单提供模拟状态切换
6. loop=false 的状态播放完后切 next
7. 缺失状态回退 idle
```

暂不做：

```text
1. 真实 AI 请求
2. 长期记忆
3. 复杂 sleep 触发策略
4. UI 美化
```

验收标准：

```text
1. 状态切换由 PetStateMachine 决策
2. FrameAnimator 只负责播放动画
3. PetWindow 不堆积复杂状态判断
4. shiroko 的多个状态动画都能被触发
```

---

## 9. 阶段 4：托盘、配置、日志

目标：

```text
补齐常驻桌面应用的基础设施。
```

只做：

```text
1. TrayController
2. 托盘显示 / 隐藏桌宠
3. 托盘退出
4. ConfigManager
5. 保存窗口位置、置顶状态、缩放、性能模式
6. Logger
7. 基础日志轮转
8. 配置损坏时备份为 .broken.json
```

暂不做：

```text
1. AI 配置界面
2. 角色包市场
3. 自动更新
4. 完整安装器
```

验收标准：

```text
1. 隐藏到托盘后动画暂停
2. 重新显示后动画恢复
3. 重启后窗口位置恢复
4. 配置损坏不会导致程序崩溃
5. 日志不会无限增长
```

---

## 10. 阶段 5：稳定性与性能检查

目标：

```text
在接入 AI 前，先确认桌宠内核稳定。
```

检查内容：

```text
1. 启动后静置 CPU 和内存
2. idle 动画连续播放
3. 隐藏到托盘后 CPU 是否下降
4. 重复显示 / 隐藏
5. 重复切换状态
6. 删除或破坏 character.json 后是否兜底
7. 删除部分状态目录后是否回退 idle
8. 切换窗口置顶和缩放是否稳定
```

验收标准：

```text
1. 空闲 CPU 占用低
2. 内存不随时间持续增长
3. 没有明显资源泄漏
4. 常见资源损坏不会崩溃
```

---

## 11. 阶段 6：AI 接入

目标：

```text
在桌宠内核稳定后，实现 OpenAI Compatible 非流式对话闭环。
```

只做：

```text
1. LLMProvider
2. OpenAICompatibleProvider
3. ConversationManager
4. Base URL / API Key / Model / Path 配置
5. QNetworkAccessManager 异步请求
6. 请求超时
7. 同时只允许一个请求
8. think -> talk -> idle
9. 失败 -> error -> idle
10. 气泡显示 AI 回复或错误提示
```

暂不做：

```text
1. 流式输出
2. 自定义 Body Template
3. 多 Provider 市场
4. 语音输入
5. 语音朗读
6. 长期记忆
```

验收标准：

```text
1. 能发送一条消息并显示回复
2. Base URL 为空时有提示
3. API Key 为空时有提示
4. Model 为空时有提示
5. 错误 API Key 不崩溃
6. 错误 URL 或超时不崩溃
7. 日志不输出完整 API Key
8. 对话历史不会无限增长
```

---

## 12. 阶段 7：UI 优化与收尾

目标：

```text
核心功能可用后，再统一处理视觉体验和易用性。
```

只做：

```text
1. 优化右键菜单结构
2. 优化气泡样式
3. 优化设置界面布局
4. 优化错误提示文案
5. 优化 README 截图和说明
6. 检查 DPI 和多屏表现
7. 整理发布包
```

暂不做：

```text
1. 新增大功能
2. 插件系统
3. 角色包市场
4. 在线素材下载
```

验收标准：

```text
1. Windows 10 / Windows 11 基本体验正常
2. 无 Qt 开发环境机器上可运行
3. README 能指导用户构建和配置
4. 素材版权、AI 隐私和 API Key 本地保存说明齐全
```

---

## 13. 当前项目进度

截至当前工作区，项目已经完成以下内容：

```text
1. 阶段 0 工程基础：
   已有 .gitignore、CMakeLists.txt、main.cpp、src/、docs/、resources/ 等基础结构

2. 阶段 1 最小桌宠窗口：
   已实现透明无边框窗口、拖动、右键退出、置顶切换

3. 阶段 2A 角色包最小读取：
   已有 CharacterPackage / CharacterPackageLoader
   能读取 shiroko/character.json 并收集状态帧路径

4. 阶段 2B idle 帧动画：
   已新增 AnimationClip / FrameAnimator
   当前实现会把当前角色包状态帧加载为 QPixmap 缓存，避免每帧读硬盘

5. 阶段 3 状态机初版：
   已新增 PetStateMachine
   已支持 idle / drag / think / talk / happy / sleep / error 的基础请求、拖动优先和缺失状态回退 idle

6. 阶段 4 基础设施的一部分：
   已新增 PetView，拆分显示职责
   已新增 TrayController，支持托盘显示、隐藏、退出
   已新增 ConfigManager，保存窗口位置和置顶状态
   已新增 Logger，支持文件日志和基础轮转
```

当前实现与计划仍存在差异：

```text
1. shiroko 角色包仍位于项目根目录 shiroko/，尚未移动到 resources/characters/shiroko
2. 当前未实现 AI 接入、AI 配置界面和 ChatBubble
3. 当前未实现 SettingsDialog
4. 当前 ConfigManager 只保存窗口位置和置顶状态，尚未保存缩放、性能模式、角色选择
5. 当前 Logger 只接入启动、退出、配置和角色包加载失败等低频日志
6. 当前 FrameAnimator 采用当前角色包全部状态帧预加载，尚未做懒加载
7. 本轮架构调整后尚未执行构建、编译或运行验证
```

---

## 14. 下一步建议

短期建议：

```text
1. 在用户确认后做一次构建验证
2. 修复构建或静态检查发现的问题
3. 补最小 README.md，记录当前开发状态、构建方式、素材版权提示
4. 确认 LICENSE 是否采用 MIT
5. 在进入 AI 前，做一次桌宠内核稳定性检查：
   - 启动显示
   - idle 动画
   - 状态切换
   - 托盘隐藏 / 显示
   - 配置保存 / 恢复
   - 日志写入 / 轮转路径
```

中期建议：

```text
1. 补 SettingsDialog 的最小框架
2. 补 ChatBubble
3. 再接入 OpenAI Compatible 非流式 AI 对话
4. AI 接入后再做 think -> talk -> idle / error -> idle 状态联动
```

---

## 15. 当前未决问题

后续开始写代码前，需要逐项确认：

```text
1. 是否创建 MIT LICENSE
2. 是否创建项目根 README.md
3. 是否把 shiroko 移动到 resources/characters/shiroko
4. 是否保持当前“预加载全部当前角色状态帧”的策略，还是改成按状态懒加载
5. shiroko 素材是否允许作为正式开源发布素材继续保留在仓库中
6. 是否在下一步执行构建验证
```
