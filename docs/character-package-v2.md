# 角色包 v2 与统一播放接口

适用阶段：B2–B4。现有 `schemaVersion: 1` PNG 包继续可用，无需迁移；v2 支持按状态混用 `frames`、`webp` 与 `alpha_video`。程序化动画仍未实现，填写对应类型会明确拒绝。

## 可用的 v2 配置

```json
{
  "schemaVersion": 2,
  "id": "my-pet",
  "displayName": "我的桌宠",
  "defaultState": "idle",
  "base": {"width": 512, "height": 512, "scale": 1.0, "anchorX": 0.5, "anchorY": 1.0},
  "bubble": {"offsetX": 0, "offsetY": -420},
  "states": {
    "idle": {
      "media": {"type": "frames", "path": "idle", "fps": 9},
      "loop": true
    },
    "happy": {
      "media": {"type": "frames", "path": "happy", "fps": 12},
      "loop": false,
      "next": "idle"
    }
  }
}
```

将 `character.json` 与 `idle/`、`happy/` 放在同一目录；各目录使用补零 PNG 文件名，例如 `000.png`、`001.png`。帧按文件名字典序排列，不递归读取。模板见 `examples/character_v2/`，该模板需要放入自己的 PNG 后才能导入。

| 字段 | v2 规则 |
| --- | --- |
| `schemaVersion` | 整数 2；同时保留 v1 读取入口 |
| `states.<状态>.media` | 必填对象；不能与旧的状态顶层 `path` / `fps` 混用 |
| `media.type` | `frames`、`webp` 或 `alpha_video` |
| `media.path` | `frames` 为 PNG 目录；`webp` 为单个 .webp 文件；`alpha_video` 为单个 .mp4 文件，均是包内相对路径 |
| `media.fps` | 仅 `frames` 使用，1–240 的整数；每帧持续 `max(1, floor(1000/fps))` 毫秒。WebP 与视频禁止填写此字段 |
| `media.layout` | 仅视频必填：`left-right` 为左 RGB、右灰度 Alpha；`top-bottom` 为上 RGB、下灰度 Alpha |
| `loop` | 可选布尔值，默认 true；由播放器统一控制 |
| `next` | 可选字符串，非空时必须指向包内已有状态；非循环播放持满最后一帧后回调一次 |

`id`、`displayName`、`author`、`version`、`preview`、`base`、`bubble`、`persona` 和 `dialogues` 延续原有含义。`idle` 与 `defaultState` 都必须存在。v2 任一状态不合法均拒绝整个包，避免导入成功后状态悄悄缺失。各类型 `media` 中未知字段也会拒绝，避免将拼错的时序配置默默忽略。

资源不允许绝对路径、盘符、`..`、冒号、空路径、符号链接或 Windows 目录联接（Junction）；各级目录与最终文件均须位于角色包内。v2 声明的 `preview` 必须存在；不提供时可省略。v1 保留缺少可选预览图的读取兼容，但导入和播放共用更严格的状态资源路径检查。

加载器只解析路径与元数据；PNG 解码保持懒加载，错误图片沿用原有跳过策略。导入验证至少要求每个状态有一张可读 PNG；如果全部解码失败，播放入口不会索引空帧列表。导出、用户人设写回不会把 v2 降成 v1。

## 播放与缓存约定

`AnimationClip` 是现有窗口和缓存管理使用的值对象，内部通过 `IPetMediaBackend` 管理媒体。PNG 实现是 `PngSequenceBackend`，负责懒加载、缩放、帧读取、卸载和解码后内存估算。复制 clip 或 `QMap` 后卸载其中一份，不影响其他副本。Qt 的像素存储仍可共享，估算是逻辑帧占用，不等于进程峰值内存。

`FrameAnimator` 统一协调暂停恢复、循环和完成通知。PNG/WebP 使用逐帧定时器，通过 `frameDurationMs(index)` 获取延迟；视频使用 `QMediaPlayer` 的异步时钟，经 `QVideoSink` 转为透明图。视频首次出帧前维持已有显示，切态不插入 idle。视频回调排队到解码回调之外执行，带播放轮次校验，旧轮次不能覆盖新状态；停止回调带对象存活检查。暂停时暂存已到达的完成/错误通知，恢复后处理。

PNG 的有效 FPS 从 clip 每帧读取，保留窗口低功耗模式对 FPS 的调整。隐藏暂停、状态预热与缓存淘汰继续通过现有窗口逻辑工作。所有 QPixmap 操作仍在 GUI 线程执行。

## WebP 配置与播放

```json
"idle": {
  "media": {"type": "webp", "path": "idle.webp"},
  "loop": true
}
```

完整、可导入的小型示例见 `examples/character_webp/`（包内也附带）。WebP 使用容器逐帧毫秒延迟，状态 `loop` 优先于容器循环次数；非循环最后一帧持满后完成并回调 `next`。动画的零/无效延迟设为 10ms，静态单帧 WebP 无延迟时设为 1000ms，避免忙循环。播放器将解码与提交耗时计入当前时隙，慢帧仍可能延迟，不保证在过载机器上精确实时。

低功耗保留 WebP 原时序，不强制拉长到 6fps，也不跳过关键帧；因此不承诺 WebP 在该模式下节电。PNG 继续使用 6fps 上限；两类媒体均沿用较慢预热与托盘隐藏暂停。隐藏后不推进解码，显示后从剩余持帧时间继续；状态重播、循环通过重开 reader 重置合成画布。停止只取消播放时钟，已缓存 reader 由原状态缓存策略保留或卸载。

`WebpAnimationBackend` 只保留当前显示 QPixmap，应用不会把全部解码帧存入列表；Qt 插件仍读取整个压缩文件，并保留合成画布及临时帧。故“逐帧”不等于按文件字节流解码或恒定 2MiB 内存。缓存估算计入压缩文件、源尺寸合成/临时画布及缩放显示帧，实际进程峰值仍需采样。

限制：单文件最多 32MiB，画布单边最多 2048px，最多 3000 帧，单帧最多 60 秒，总时长最多 5 分钟。头部尺寸先于解码扫描检查。导入/导出验证逐帧解码整文件，迟到的损坏帧也会拒绝；验证成本随素材增加，不是后台导入。播放时文件改变或损坏会停止该轮、显示错误，非 idle 状态尝试回 idle；idle 自身损坏时保留最后显示图，不进行无限重试。不能用 PNG 等其他格式改后缀伪装 WebP。

v1 仍只支持 PNG，不会因新增后端改变原素材、FPS 或路径配置。轻动效通过顶层 effects 叠加，仍拒绝 procedural 等第四媒体类型。

## Alpha-Matte MP4 配置与播放

```json
"happy": {
  "media": {"type": "alpha_video", "path": "happy.mp4", "layout": "left-right"},
  "loop": false,
  "next": "idle"
}
```

首版支持本地、自包含、单视频轨的普通 H.264/avc1 MP4。RGB 为未预乘颜色，遮罩为灰度 Alpha（黑透明、白不透明）；合成后转为预乘 ARGB，再按角色大小缩放。首版统一静音，不创建音频输出；不能填写 `fps` 或其他未开放的音频选项。最小可导入示例为 `examples/character_alpha_video/`。

容器限制：文件不超过 128MiB；元数据 moov 不超过 4MiB；拼接画布宽高均为偶数且不超过 2048，拼接轴为 4 的倍数；时长最多 300 秒、最多 18000 帧，每帧间隔 1/60–1 秒。拒绝外部媒体引用、旋转/变换轨道、分片 MP4、其他编码及越界块/截断元数据。路径沿用包内和符号链接/Junction 限制。

导入/加载检查容器和边界，不在 GUI 线程同步播放整段视频，也不等同于逐帧解码完整性保证。播放前重查容器；实际帧尺寸/方向与头部不符、解码失败或连续 10 秒无帧会停止并报告；非 idle 尝试回 idle，idle 错误保留已有显示，不无限重试。这些检查不是解码器安全沙箱，Qt/系统媒体组件仍需正常维护。

隐藏默认暂停播放器并保留当前显示帧，恢复继续；停止/切态销毁播放器以释放解码资源，已缓存的当前帧仍可由缓存管理卸载。预热仅检查视频容器，不后台启动所有视频。低功耗保留视频原时序，不拉长动作，也未承诺降低解码开销。解码器/GPU 内存不包含在应用当前显示帧估算中，性能须看进程采样。首帧和循环的实际延迟依赖机器与 Qt 后端，不能承诺所有电脑硬解或绝对无接缝。

本轮真实 512px 素材触发 Qt 6.5.3 FFmpeg 硬解 surface pool 耗尽，首版默认软件解码：进程内未显式设置时，将 `QT_FFMPEG_DECODING_HW_DEVICE_TYPES` 设为 `,`，在首次解码前禁用硬件设备列表。不修改用户/系统环境或个人配置；显式环境覆盖仍保留供诊断。此策略不是运行中逐播放器自动切换硬/软解，CPU 成本按软解实测，硬解稳定性仍待后续验证。

## 依赖与发布

发布使用匹配 Qt Kit 的 qwebp 插件，链接固定 libwebp 1.6.0；准备脚本 `tools/build_webp_plugin.ps1` 校验源码归档 SHA256，在独立目录构建，不安装到全局 Qt Kit。不能直接将 B2 基于附带旧 libwebp 的探针 DLL 当正式发布依赖。

打包脚本接受 `-WebpRuntimeDir`（默认 `build/webp-runtime/bundle`），缺少插件、版本/哈希不匹配、许可或源码归档缺失均中止。`third_party/webp/` 随 ZIP/安装器包含 Qt 插件许可、libwebp 版权/专利说明、对应源码和构建脚本；项目 MIT 许可不替代第三方条款。用户无需开发环境。

`tools/verify_release.ps1` 在空 PATH 下执行主程序 `--check-webp-runtime`，用随包四帧样本做真实解码检查；该入口在单实例激活、日志和个人配置读取之前返回，不会唤醒已有桌宠。依赖检查不代替干净 Windows 安装、声音、HTTPS 或其他客户端验收。

视频继续使用匹配 Kit 的 Qt Multimedia 后端及其依赖，由 `windeployqt` 部署，完整包必须通过 `--check-video-runtime` 的真实 H.264 解码、透明度与完成事件自检。样本和离线说明随包提供，不依赖用户安装 FFmpeg 命令行或开发环境。编码工具只用于开发测试，不随包作为运行依赖。


## 可选角色轻动效（B5）

```json
"effects": {
  "breathing": {"states": ["idle"], "amplitude": 0.008, "periodMs": 3600},
  "dragSway": {"maxAngleDeg": 4},
  "blink": {"state": "idle", "closedFrame": "effects/idle-closed.png", "intervalMs": [4000, 8000], "holdMs": 120},
  "statusBadge": {"enabled": true}
}
```

仅 v2 可声明，缺项即关闭；旧包无效果、不启装饰时钟。设置页全局“角色轻动效”开关默认开启，但只影响已声明角色。呼吸幅度 0.1–2%、周期 1500–10000ms，states 必须存在且不重复；摆角 0.1–6 度，松手最多 800ms 回正。默认呼吸/摆动 20/30fps，低功耗 10/15fps。

眨眼仅限单 PNG 的 idle，专用完整闭眼 PNG 须与源图同画布；不适用于 WebP/视频/多帧 idle，不生成眼睛、不压扁角色。随机间隔为 1000–60000ms 整数区间、持帧 50–300ms；推荐 4000–8000 与 120ms。角标只显示一个：think 省略号，待确认或错误感叹号，sleep 为 Zzz。角标不重复 Agent 数量，不播放动态角标。

固定透明边距容纳最大变换，气泡和胶囊使用稳定内容锚点；点击/摸头逆变换，隐藏停止装饰时钟，恢复不补播过期效果。完整原创几何示例见 examples/character_effects，白子示范不含虚构闭眼素材。

导入预检：清单 ≤1MiB、≤64 状态、包 ≤1GiB、≤20000 项、≤16 层目录；禁止符号链接/Junction。视频校验包括采样到块映射及完整采样范围。作者工具可完整离线解码后导出，详见 character-studio.md。
