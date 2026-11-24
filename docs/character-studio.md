# 角色工坊使用说明

`QtDesktopPetCharacterStudio.exe` 是独立作者工具。完整解压作者包后直接启动，随包携带 Qt、Windows C++ 运行库、固定 FFmpeg/ffprobe、许可与对应源码/构建材料；用户不需要安装 Python、Node、FFmpeg 或 Qt。普通桌宠安装包不包含编码器。

## 图形向导

1. 选择 PNG 文件、按文件名排序的 PNG 序列目录、透明 ProRes 4444 MOV、VP9 Alpha WebM、绿幕 H.264 MP4/MOV，或已有 v1/v2 角色目录。序列文件请使用补零编号。
2. 已有角色包只勾选需要转换的状态；人设、台词、锚点、next 和未选素材保留。设置全新的输出目录，工坊拒绝覆盖和源/输出嵌套。
3. 选择 Animated WebP、Alpha-Matte MP4 或单 PNG。最大逻辑边默认 512，保持比例，必要时补透明边对齐。序列填写真实源帧率；自动模式保留 ≤30fps 的源帧率，超过默认 30fps，可手动指定 1–60fps。
4. 绿幕勾选抠像、取色并调整相似度、过渡、去绿边。更新首帧对照，在棋盘/浅色/深色背景检查。预览与导出使用相同参数，但不能保证复杂发丝、半透明材质完美。
5. 单 PNG 可声明呼吸、拖拽摆动、角标；眨眼必须另提供同画布完整闭眼 PNG，只支持静态 idle，不能以压扁原图代替。
6. 加入队列后串行转换。取消会结束当前编码进程并清理任务临时目录，已成功导出的目录保留。转换通过完整离线解码和角色校验后才发布输出目录。

WebP 的“无损”针对缩放、抠像后的归一化帧；缩放和抠像会改变原始素材。MP4 使用静音 H.264 yuv420p CRF12，存在有损边缘；保留原始母版。此工具不做 AI 补帧。导出后需在桌宠导入并检查完整动作、透明边缘及气泡锚点。

## CLI 与配方

```powershell
.\QtDesktopPetCharacterTool.exe self-check
.\QtDesktopPetCharacterTool.exe probe --input D:\art\role
.\QtDesktopPetCharacterTool.exe convert --recipe D:\art\task.json
.\QtDesktopPetCharacterTool.exe validate --input D:\art\exported-role
```

示例配方：

```json
{
  "schemaVersion": 1,
  "input": "D:/art/idle-png",
  "output": "D:/art/exported-role",
  "id": "my-character",
  "format": "webp",
  "sourceFps": 12,
  "fps": 0,
  "maxSide": 512,
  "layout": "left-right",
  "key": {"enabled": false, "color": "#00ff00", "similarity": 0.12, "blend": 0.06, "despill": 0.2}
}
```

已有角色目录需增加 `"states": ["idle", "happy"]`。`format` 为 `frames` 时只接受归一化后单帧，主要用于静态角色。轻动效通过 `effects` 声明，格式见角色包 v2 说明；`closedFrameInput` 为闭眼 PNG 本地路径。CLI 标准输出为逐行 JSON 进度与最终结果，Ctrl+C 取消。

退出码：0 成功、2 输入不合规、3 依赖缺失/哈希错误、4 转换失败、5 产物验证失败、130 取消。

## 限制与离线交付

仅接受本地文件与目录，禁止 URL、播放列表、链接/Junction、越界和嵌套输出。角色包清单 ≤1MiB、≤64 状态、≤1GiB、≤20000 项、≤16 层目录；视频遵循桌宠播放器 300 秒、18000 帧、128MiB、自包含 H.264 等限制，超过明确报错，不静默截断。

作者包中的编码器是独立 GPL 程序，通过子进程调用；许可、源码、补丁/构建参数见 `encoder/`。不要删除依赖材料或仅拷贝 EXE。制作分发包时任何哈希或材料缺失都会失败。工具本身不读写桌宠个人设置、不连接 Agent、不调用网络模型。

实际安装、干净 Windows 和完整人工视觉检查，以本次测试记录中的状态为准；编译成功不能替代这些验收。
