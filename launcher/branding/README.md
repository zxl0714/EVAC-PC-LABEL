# 启动器标识

用户提供的 EVAC PC LABEL 完整标识用于启动器左上角；EXE、标题栏和任务栏图标
使用保留 EVAC＋PC LABEL 两行字标的版本，不单独使用星形。窗口标题和启动文件名
仍为 CAVE PC Collection。

原附件是 1280 × 1280 的不透明 JPEG。`evac-logo.png` 和 `evac-icon.png`
由内置图像工具生成透明底切图，保留原有字样、配色和标记；来源和资源哈希见
`../branding.json`。Logo 与 ICO 都嵌入 EXE，不需要在玩家目录放置散装图片。

Logo 的处理要求：去掉白底与文字内部白色区域，保留 `EVAC`、`PC LABEL`、
`THE CAVE RECOMPILE PROJECT`、原字体形状及颜色；去除多余上下留白，不加描边或阴影。
图标的处理要求：保留 EVAC 和 PC LABEL，星标仍位于原字母 A 内；去掉最底下的
微小说明字，缩紧留白并居中放在透明正方形画布，保持字形、颜色和宽高比，
不添加底板、描边或阴影。

`evac.ico` 包含 16／20／24／32／40／48／64／128／256 像素的 32 位透明图像。
需要更新 ICO 时，从仓库根运行以下命令，先输出新文件，再同步清单哈希：

```powershell
./scripts/Build-Icon.ps1 -InputPng launcher/branding/evac-icon.png -OutputIco '<新的 .ico 路径>'
```

构建和源码边界审计只允许清单内通过 PNG／ICO 结构与哈希核对的标识资源。
