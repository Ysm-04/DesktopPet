# WebP 最小示例

本目录是可以导入的完整示例：四帧彩色方块由本项目测试脚本生成，没有第三方角色素材。

在设置页验证、导入并切换到“WebP 示例方块”，可以检查透明动画。制作自己的包时替换 WebP 文件、修改 id 和显示名称；按各状态分别选用 `frames` 或 `webp`。现有 v1 PNG 包无需迁移。

WebP 的 `media` 只填写 `type` 和包内文件 `path`，不要填写 `fps`；每帧时长来自文件本身。状态 `loop` 决定是否循环，单次动作可加 `"next": "idle"`。低功耗不拉长 WebP 动作；隐藏到托盘时暂停解码，恢复继续。

请完整保留发布目录的 `imageformats/qwebp.dll`、Qt/VC 运行库和 `third_party/webp/`。普通用户无需安装 Qt、编译器、Python 或下载额外 DLL。详细规则见随包 `使用说明.md`。
