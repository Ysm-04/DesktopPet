# v2 PNG 角色配置模板

复制本目录的 `character.json` 到自己的角色目录，创建 `idle/` 和 `happy/`，放入有权使用的 PNG 序列（`000.png`、`001.png`……），再从设置页验证和导入。此目录只有配置，没有附带角色素材，不能直接作为完整角色包导入。

旧 v1 角色包不需要修改。本模板使用 `media.type: "frames"`；WebP 示例另见 `examples/character_webp/`，视频尚未接入。路径相对于角色包根目录，不得引用目录外资源或符号链接。基本配置见根目录 `使用说明.md`；开发仓库完整接口规范见 `docs/character-package-v2.md`。
