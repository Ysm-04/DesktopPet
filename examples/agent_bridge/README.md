# 外部 Agent 联动配置指南

更新时间：2026-11-24。三端基础联动于 2026-11-23 完成真实客户端验收；本次状态收尾与 Codex 暂停兼容修复的真实客户端复测单独追踪。

本目录随便携包和安装包提供；使用它不需要下载源码、安装 Qt 或配置开发环境。Windows 10/11 64 位用户先安装/完整解压 QtDesktopPet，再自行安装并配置需要联动的 Agent 客户端。

## 第一步：确认桌宠本地通道

1. 启动 `QtDesktopPet.exe`，右键桌宠打开 **设置 → 应用**。
2. 启用「外部编程 Agent 状态联动」和「显示桌宠底部状态胶囊条」。
3. 在「快速接入外部编程 Agent」点击「测试联动」。成功后再配置客户端。

发布包已经提供 `QtDesktopPetBridge.exe`、匹配的 Qt DLL/插件和编译器运行库。请保留完整目录，不要只移动 EXE，也不需要把 Qt 添加到 PATH。安装或首次启动桌宠不会静默改写个人 Agent 配置。

| 随附文件 | 用途 |
| --- | --- |
| `codex.hooks.json` | Codex 手动合并参考；推荐用设置页一键接入 |
| `antigravity.hooks.json` | Antigravity 手动合并参考；推荐用设置页一键接入 |
| `dsh-cordis-plugin.js` | DSH 的 Cordis 只读观察插件 |
| `test_bridge.ps1` | 三阶段本地收件自检，不修改 Agent 配置 |

## Codex：一键接入与信任

1. 在桌宠设置页 Codex 行点「一键接入」；检查确认框中的配置路径，再确认写入。已有文件会先备份，其他 Hook 会保留。
2. 默认配置在 `%USERPROFILE%\.codex\hooks.json`。如果桌宠进程继承了 `CODEX_HOME`，使用该目录下的 `hooks.json`；请确认它与 Codex 实际使用的 home 一致。修改环境变量后要重启桌宠和 Codex。
3. 在 Codex 中按提示信任这些 Hook。命令、匹配范围或路径改变后，可能需要重新确认。桌宠的「已接入」只表示配置和路径匹配。
4. 必要时重启 Codex，在新/现有对话执行一次查看目录等只读工具操作，检查胶囊详情中是否有对应活动。

配置使用 `SessionStart`、`UserPromptSubmit`、`PostToolUse`、`Stop`，以及新版本支持的 `Interrupt`。`PostToolUse.matcher` 应为 `*`，否则读取、搜索及其他工具可能被过滤。使用支持这些 Hook 的 Codex 版本；如安装了 CLI，可用 `codex features list` 检查当前版本的 `hooks` 特性状态。

**手动停止输出**：新版模板的 `Interrupt` 将对应对话标为“待你确认 / 已暂停，等待继续”，解除其工作动画占用；它不代表桌宠能恢复该任务，继续操作仍在 Codex 中完成。已接入旧模板的用户需在升级桌宠与桥接器后，再从设置页确认「一键接入 / 修复路径」，并信任新增 Hook。仅替换 EXE 不会自动更新个人配置。

2026-11-24 隔离加载检查中，桌面版捆绑的 Codex `0.171.0-alpha.13` 能加载 `Interrupt`，独立 CLI `0.134.0` 忽略此事件且仍加载其他 Hook；两者版本可能不同。该检查没有模拟用户真实点击停止。若客户端不提供中断事件，桌宠无法立即判断手动停止，超过状态 TTL 后只标为“待同步”，不会伪造“已暂停”或“已完成”。升级后仍应手测一次“停止 → 继续 → 正常结束”。

仍有任一未隐藏且未过期的对话处于 working 时，桌宠保持 think，State Test 菜单锁定；全部工作结束后释放锁定，正常完成播放 happy 后回 idle，出错播放 error 后回 idle。仅待确认/暂停时不锁菜单；本地聊天、拖拽与提醒优先。

不要用桌宠配置覆盖 `config.toml` 的 `notify`，也不要手工写入信任哈希来绕过客户端授权。不能只凭 `hook/completed` 或桥接默认退出码 `0` 判断投递成功。

## Antigravity：一键接入

1. 在桌宠设置页 Antigravity 行点「一键接入」，检查目标文件后确认。全局配置为 `%USERPROFILE%\.gemini\config\hooks.json`。
2. 必要时重启 Antigravity，然后执行一次只读工具操作并展开桌宠详情。
3. 若项目有 `.agents/hooks.json`，检查是否重复配置本项目的 Hook；避免全局和项目级重复触发。

模板使用 `SessionStart`、`PreInvocation`、`PostToolUse`、`Stop`。保留 `--stdin-sniff` 和 `--stdout-json "{}"`。本项目不挂载 `PreToolUse`，不输出 `decision: allow`，以免干涉原客户端的权限审批。

优先让设置页生成命令，尤其是安装路径含空格或中文时。Codex 与 Antigravity 的命令执行环境可能不同，不要将一端的手工引号规则直接套到另一端。

## DSH：配置个人 profile

本插件按 DSH `0.2.1-rc.2` 的 Cordis 会话接口核对并完成实测。其他版本如改变事件接口，需要重新验证。插件由 **DSH 自带的 Node 环境**加载，只使用 Node 内置模块，不需要额外安装 Node.js、npm 依赖或 Qt。

1. 在桌宠设置页点「打开模板目录」，找到 `dsh-cordis-plugin.js`。该文件应保留在桌宠的固定安装目录内。
2. 找到 DSH 正在使用的个人 profile。桌面版常见路径为 `%USERPROFILE%\.dsh\profiles\desktop\`，其他 profile 使用各自目录。先备份其中的 `cordis.patch.yml`；不存在时新建该文件。
3. 向 `cordis.patch.yml` 的顶层补丁数组**追加一次**下面的插入项。不要覆盖原有内容，不要重复添加同一 `id`，也不要直接改生成的 `cordis.yml`。

```yaml
- insert:
    - id: qt-desktop-pet-status
      name: "file:///D:/Apps/QtDesktopPet/examples/agent_bridge/dsh-cordis-plugin.js"
      config:
        bridgeExePath: "D:/Apps/QtDesktopPet/QtDesktopPetBridge.exe"
```

`name` 是插件的文件 URL，`bridgeExePath` 是 EXE 的绝对文件路径，两处都要替换。YAML 使用空格缩进。推荐路径中使用 `/`；如果在双引号内用 Windows `\`，需按 YAML 规则写成 `\\`。

**路径含空格或中文时**，可在 Windows PowerShell 中生成正确的文件 URL（只输出 URL，不改配置）：

```powershell
$pluginPath = 'C:\Users\你的用户名\AppData\Local\Programs\QtDesktopPet\examples\agent_bridge\dsh-cordis-plugin.js'
([System.Uri](Get-Item -LiteralPath $pluginPath).FullName).AbsoluteUri
```

将输出原样填入 `name`。`bridgeExePath` 仍填普通绝对路径，不填 URL。如果已有 `qt-desktop-pet-status` 插入项，只更新原项。

4. 完全退出并重新打开 DSH，触发一次只读工具操作，在桌宠查看 **DSH → 对话 → 活动**。只打开 DSH 而未开始任务时，没有胶囊是正常的。

插件观察 `session/event(session, event)` 和 `session/disposed`，识别回合开始、工具调用/结果、权限等待/决定与回合结束；不订阅或修改 `approval/request`。不转发工具输出、模型消息或私有 meta。插件不替用户作权限决定。

## 详情如何阅读

- 每种 Agent 一个胶囊；同一种来源的多个对话归到同一胶囊。胶囊最多展示 4 种来源，更多来源从 `+N` 进入详情。
- 详情按 **Agent → 对话 → 活动** 展开，活动按命令、读取、搜索、修改等分类。同一次工具调用的开始/结果更新同一条，不叠加对话计数。
- 有标题时显示标题，否则用工作区名和短 ID；不会读取提示词来编造标题。缺少稳定会话 ID 时可能显示未识别对话。
- 每对话最多保存 50 条，默认最近 5 条，可以「查看全部」。结束/过期对话约保留 10 分钟，每来源最多保留 20 个最近对话；仅存内存，重启桌宠会清空。
- 「已结束」表示没有明确成功/失败结果；「待同步」表示上游后续事件未到达。单条工具失败不等于整个对话失败。只能展示客户端实际上报的工具，不能保证覆盖所有托管工具或后台任务。
- 右键可隐藏对话，底部可「清除已结束」「清空」；这些操作仅影响显示，不取消外部任务。新状态可能再次显示该对话。
- 气泡推荐用「仅待确认与异常」，提示音可选；有其他对话仍在运行时，不会因一个对话完成就整体庆祝。

## 通道自检与排障

从安装/解压目录打开 PowerShell，先启动桌宠，再运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\examples\agent_bridge\test_bridge.ps1" `
  -BridgeExePath ".\QtDesktopPetBridge.exe"
```

自检发送 `working → needs_input → done` 三个事件，使用 `--strict` 检查是否写入桌宠管道。输出 PASS 后还应观察胶囊的状态变化。自检通过证明本地通道可用，客户端的配置、信任、事件触发要另查。

| 现象 | 检查 |
| --- | --- |
| 自检失败、Pipe 不可达 | 桌宠是否启动、联动是否开启，是否使用同一 Windows 用户和默认管道 `QtDesktopPet.AgentStatus.v1`；尽量两端均以普通用户运行 |
| 缺 Qt DLL、VC DLL 或 `0xC0000135` | 完整重新解压/安装；检查安全软件隔离记录。不要依赖开发机 PATH，不要从不明网站补 DLL |
| 自检成功、实际客户端无胶囊 | Hook 是否加载/受信任、实际配置 home 是否一致、是否需重启、是否真正触发工具；DSH 检查 profile、YAML 和文件 URL |
| 有状态但没有工具子条目 | Codex 的 matcher 是否为 `*`，两端参数是否保留 `--stdin-sniff`，DSH 是否更新为本目录插件；核对上游版本是否支持该事件 |
| 出现未识别或重复对话 | 检查旧 Hook/项目级 Hook 是否重复，以及上游事件是否含稳定 session/conversation ID |
| 移动或升级后失效 | 完整更新发布目录；Codex/Antigravity 点「修复路径」并按需重新信任，DSH 更新 URL 与 EXE 路径后重启 |

日常 Hook 不使用 `--strict`：桌宠没启动时应安静退出，不阻断 Agent。后台调用隐藏窗口；桥接是控制台程序，手动双击可能一闪而过，这是正常的。

## 升级与解除接入

同目录升级：先退出桌宠，完整更新文件，再检查联动。移动目录后必须修复绝对路径。卸载桌宠前，在设置页解除 Codex/Antigravity 接入；备份 DSH profile 后，仅移除本项目 `qt-desktop-pet-status` 插入项。安装器和卸载器不会擅自清理第三方配置。

上报内容仅在本机流向桌宠，不传给桌宠配置的 AI 服务；活动摘要可能含命令/文件名，内置脱敏不能保证覆盖所有秘密格式，请勿主动传入凭据、完整输出、环境变量或聊天正文。

## 自定义 CLI

在安装目录使用稳定的会话标识调用桥接：

```powershell
.\QtDesktopPetBridge.exe --source custom --session my-task --workspace MyRepo --state working --summary "正在构建"
.\QtDesktopPetBridge.exe --source custom --session my-task --workspace MyRepo --state done --summary "构建完成"
```

支持 `working`、`needs_input`、`done`、`error`、`idle`。分层活动可用 `--packet-stdin` 传递规范化 JSON；完整开发协议见开发仓库的 `docs/QtDesktopPet_外部Agent接入指引.md`（该内部文档不随用户包提供）。
