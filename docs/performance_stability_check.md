# 性能与稳定性检查记录

本文档用于记录可重复的阶段性性能与稳定性检查。采样脚本和本记录只面向开发与回归测试，不进入普通用户发布包。

## 测试环境

| 项目 | 内容 |
| --- | --- |
| 测试日期 | TODO |
| Git commit | TODO |
| 构建类型 | TODO: Debug / Release |
| 操作系统 | TODO |
| CPU | TODO |
| 内存 | TODO |
| 显示器 / DPI | TODO |
| Qt 版本 | TODO |
| 角色包 | `resources/characters/shiroko` |
| AppConfig 关键项 | scale=TODO, performanceMode=TODO, pauseWhenHidden=TODO, enableLazyLoad=TODO, enableAnimationPrewarm=TODO, animationCacheLimitMb=TODO, unloadAnimationsWhenHidden=TODO |

## 采样脚本

默认采样当前 `QtDesktopPet` 进程 5 分钟，每 5 秒一条：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/perf_sample.ps1
```

指定 PID 采样：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/perf_sample.ps1 -Pid 12345 -DurationSeconds 600
```

输出目录默认是：

```text
reports/perf/
```

`reports/perf/` 已加入 `.gitignore`。需要沉淀结论时，只把摘要写入本文档，不提交原始采样 CSV。

## 检查项

| 场景 | 步骤 | 采样命令 | 预期结果 | 实际结果 | 结论 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 启动后静置 | 启动程序后不操作 5 分钟 | `tools/perf_sample.ps1` 默认参数 | CPU 保持低占用，内存无持续上涨 | TODO | TODO | TODO |
| idle 连续播放 | 保持桌宠可见并播放 idle 10 分钟 | `tools/perf_sample.ps1 -DurationSeconds 600` | 动画持续播放，内存曲线稳定 | TODO | TODO | TODO |
| 隐藏到托盘 | 可见采样 3 分钟，隐藏后再采样 3 分钟 | 两次采样分别记录 | 隐藏后 CPU 下降或保持低占用 | TODO | TODO | TODO |
| 重复显示 / 隐藏 | 连续显示/隐藏 10 次 | `tools/perf_sample.ps1 -DurationSeconds 300` | 无崩溃，句柄数和内存无异常增长 | TODO | TODO | TODO |
| 重复切换状态 | 通过右键状态测试切换 `idle/think/talk/error/drag` | `tools/perf_sample.ps1 -DurationSeconds 300` | 首次切换状态允许加载资源，缓存达到上限后按 LRU 卸载非保护状态 | TODO | TODO | 结合日志确认加载/卸载记录 |
| 动画预热与卸载 | 默认配置启动后静置，随后隐藏到托盘再显示 | `tools/perf_sample.ps1 -DurationSeconds 600` | 日志出现有限次分批预热；隐藏后非保护动画缓存释放；显示后不会反复预热刚被卸载的状态 | TODO | TODO | 不应影响当前播放状态恢复 |
| 缩放 / 置顶切换 | 设置页切换缩放，右键切换置顶 | `tools/perf_sample.ps1 -DurationSeconds 300` | 窗口尺寸和状态稳定，无崩溃 | TODO | TODO | TODO |
| AI 对话 | 连续发送 20 轮短消息 | `tools/perf_sample.ps1 -DurationSeconds 900` | 请求期间 UI 不阻塞，内存不持续上涨；日志不包含完整消息正文、API Key 或完整错误响应正文 | TODO | TODO | 使用错误 Key/Base URL 时检查脱敏摘要 |
| 配置损坏兜底 | 备份后分别破坏 app 配置、AI 配置或本地聊天记录再启动 | 启动后采样 3 分钟 | 程序恢复默认配置或忽略损坏历史，并生成带时间戳的 broken 备份，不覆盖旧备份 | TODO | TODO | 使用备份副本测试 |
| 角色包损坏兜底 | 使用临时复制的损坏角色包测试 | 启动后采样 3 分钟 | 程序不崩溃，回退 preview 或默认显示 | TODO | TODO | 不直接破坏仓库内默认角色包 |

## 懒加载现状

当前实现是状态级懒加载、主线程分批预热和 LRU 卸载：

```text
1. enableLazyLoad=true 时，构建 AnimationClip 只保存帧路径
2. 某个状态首次播放时调用 ensureLoaded() 加载该状态帧
3. enableAnimationPrewarm=true 时，启动后在主线程按批次预热常用状态
4. 已加载状态按状态级 LRU 策略管理，超过 animationCacheLimitMb 时卸载非保护状态
5. 单轮预热记录会避免反复重新加载刚被 LRU 卸载的状态
6. unloadAnimationsWhenHidden=true 时，隐藏到托盘会释放非保护动画缓存
7. enableLazyLoad=false 时，仍保持启动阶段加载全部状态帧的兼容行为
```

尚未实现：

```text
1. 后台线程预热
2. 单帧级缓存
3. 自动化长期压测记录
```

## 发布包说明

普通用户发布包不包含：

```text
tools/
docs/
reports/
build/
.git/
```

普通用户发布包应包含运行必需内容：

```text
QtDesktopPet.exe
Qt 运行时依赖
resources/characters/
resources/icons/
LICENSE
README.md
```

运行时资源查找顺序：

```text
1. QtDesktopPet.exe 同级 resources/
2. 开发源码目录 resources/
```
