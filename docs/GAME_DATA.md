# 游戏数据指南

[简体中文](GAME_DATA.md) | [English](GAME_DATA.en.md)

按需准备要玩的游戏数据即可。

## 先选哪一种放法

- **使用现有目录**：在启动器选中游戏，打开“游戏数据…”→“选择数据目录…”。这是路径设置，不会移动、修改原始文件。
- **集中放入合集**：把游戏文件放到对应的 `game-data/<游戏 ID>/`，然后在启动器点“刷新”。若此前设置过外部目录，再用“选择数据目录…”切换到这里。
- **由启动器复制**：“游戏数据…”→“复制到合集…”会检查并复制所选文件夹的全部内容，原目录保留。目标游戏目录须为空或仅含 `README.txt`，也要有足够磁盘空间；已有说明会保留，原目录另备份到该游戏缓存。

下面示例中的游戏目录，就是选择文件夹时应选的那一层。示例只列出辨认层级的文件，省略号代表仍须保留的其他原始资源。

## 按目录放置：先看这四步

1. **先解包。** ISO／PKG 需要提取成游戏文件；ZIP 如果包含已提取的文件，完整解压即可，如果里面仍是 ISO／PKG，还要继续提取。不要直接把镜像或压缩包放进数据目录，也不要仅修改扩展名。
2. **找到游戏根目录。** Xbox 360 作品以直接包含 `default.xex` 的那层为准，PS4 作品以直接包含 `eboot.bin` 的那层为准。
3. **复制里面的全部内容。** 放到对应的 `game-data/<游戏 ID>/`，保留全部资源的文件名和相对层级，不要只复制入口程序。需要更新的作品按下面各自的版本要求处理。
4. **在启动器点“刷新”。** 选择游戏并启动。若此前选择过外部目录，先通过“游戏数据…”→“选择数据目录…”改选新位置。

以虫姬二为例：

```text
正确：game-data/futari/default.xex
      game-data/futari/media/...

多套一层：game-data/futari/解压目录/default.xex
```

已经多套一层时，可以直接在启动器选择那个实际包含入口文件的目录，无须重新解压。下载的压缩包名称、通过启动器选择的外层目录名称不影响识别；默认 `game-data/<游戏 ID>/` 路径及游戏内部文件名则按本文保留。程序会检查实际内容和版本，不是仅按名字识别。

## ISO 和 PKG 解包

已经有 `default.xex`／`eboot.bin` 和完整资源目录时，无须再次解包。ZIP／RAR 用常规解压软件完整展开；若得到 ISO／PKG，再按下面处理。

### Xbox 360：extract-xiso

1. 下载 [XboxDev/extract-xiso 的 Windows Release](https://github.com/XboxDev/extract-xiso/releases)，完整解压工具 ZIP，使用其中的 `extract-xiso.exe`。
2. 在工具所在目录打开 PowerShell，替换命令中的输入文件和新输出目录：

```powershell
.\extract-xiso.exe -x -d "D:\GameExtract\futari" "D:\Games\futari.iso"
```

3. 在启动器选择 `D:\GameExtract\futari`，或将里面全部内容复制到 `game-data/futari/`。G2／最大往生同理，使用各自的目录。原 ISO 保留；只提取文件，不需要重建或改写镜像。

### PS4：PkgTool 与配套提取脚本

1. 下载 [LibOrbisPkg v0.2](https://github.com/maxton/LibOrbisPkg/releases/tag/v0.2) 中的 `PkgTool-0.2.231.zip`，完整解压，保留 `PkgTool.exe` 旁的 DLL。不要只复制 EXE。
2. 在合集根目录打开 PowerShell，运行包内 `tools/Extract-Ps4Pkg.ps1`；源码仓库中同一脚本在 `scripts/`。分别提取本体和所需更新，两个输出目录须尚不存在：

```powershell
.\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\base.pkg" -OutputDir "D:\GameExtract\base"
.\tools\Extract-Ps4Pkg.ps1 -PkgTool "D:\Tools\PkgTool\PkgTool.exe" -InputPkg "D:\Games\update.pkg" -OutputDir "D:\GameExtract\update"
```

3. 提取成功后，脚本打印 `Game files: ...\uroot`。以 ESP Ra.De. 为例，先把 `base/uroot/` **内部所有文件和文件夹**复制进 `game-data/esprade/`，再把 `update/uroot/` 内部所有内容覆盖进去，替换同名文件。保留本体独有资源，不要用更新目录整体替换本体目录。
4. 最终直接看到 `game-data/esprade/eboot.bin` 和 `game-data/esprade/sce_sys/param.sfo`；在启动器选择这一层并启动校验。其他 PS4 游戏使用各自支持的版本与默认目录。

脚本补齐普通 `pkg_extract` 未导出的 PKG 元数据，避免只得到 `uroot` 资源却缺 `sce_sys`。它只写入新建的输出目录，原 PKG 不变；不使用会重新生成 SFO 的 `pkg_makegp4`。

默认 passcode 是常见备份包的 32 个零。若备份使用其他 passcode，在命令末尾加 `-Passcode "对应的32字符"`。网站 ZIP／RAR 的解压密码只用于外层压缩包，不等于 PKG passcode；零售加密 PKG 需先准备可提取的备份。工具报错时先查看输出，不要把未提取完整的目录当作成品。

### Xbox 360 XBLA：狱门山

狱门山使用 XBLA 内容包，不用 `extract-xiso`。ZIP／RAR 解压后，如果得到一个长文件名、没有扩展名的大文件，它仍是容器。在合集根目录使用 v0.7 随包脚本，替换成实际文件路径：

```powershell
.\tools\Extract-Xbla.ps1 -InputPackage "D:\Games\原始容器文件名" -OutputDir "D:\GameExtract\guwange"
```

Windows PowerShell 5.1 即可运行，无须另装 Python。输出目录须尚不存在，上一层 `D:\GameExtract` 须已创建。源码仓库中脚本位于 `scripts/Extract-Xbla.ps1`。它提取本作使用的 LIVE／PIRS 单哈希表容器，检查文件链与数据块校验，原容器保留；不用于 ISO、CON 存档或应用 Title Update。

完成后直接选择 `D:\GameExtract\guwange`，或把里面全部内容复制到 `game-data/guwange/`。不要把整个容器改名成 `default.xex`。

## Xbox 360 游戏

### 虫姫さまふたり Ver.1.5（futari）

```text
game-data/futari/
  default.xex
  media/
    Module/
    ...
  ...
```

使用完整的 Xbox 360 Ver.1.5 数据。Black Label 和 Ver.1.01 为可选 DLC，不是游玩本体的前提。没有 DLC 时，`dlc/` 可以留空或不存在，不必修改配置；Ver.1.5 本体仍可运行，Black Label 不可选、Ver.1.01 不显示。

DLC 默认查找本体目录下的 `dlc/`。把两个原始 DLC 文件放到以下位置，保留文件名，随后启动游戏：

```text
game-data/futari/dlc/
  0000000000000000/
    435A07D7/
      00000002/
        496F816DD4B53DAEFF931838B6C69831AAD7663043
        9893A78CA6062B935BD1F53EA43CFD546B7246DF43
```

`496F...` 是 Black Label，`9893...` 是 Ver.1.01，两项可分别安装。首次启动会自动解包，之后直接使用准备好的内容。

已有 v0.7.2 的解包目录保持原样即可，`dlc/00000002/<包目录>/` 也能识别。已解包的 Black Label 包目录包含 `spa.bin` 和 `Rom/cg_black.bin`、`Rom/cg_blackhi.bin`、`Rom/dropprm.bl`；Ver.1.01 包目录包含 `ver101_key.bin`。

默认配置会跟随启动器所选的本体目录：

```ini
[game.futari]
dlc_dir = auto
```

例如本体选为 `D:/Games/Futari`，就查找 `D:/Games/Futari/dlc`。旧配置中已经写明的路径会继续使用；要跟随本体，将其改为 `auto`。使用其他 DLC 位置时填写具体路径：

```ini
[game.futari]
dlc_dir = D:/Games/Futari/DLC
```

选中虫姬二后点击启动器底部“详情”，可以看到实际 DLC 查找目录及 Black Label／Ver.1.01 的识别状态。显示缺文件或哈希不匹配时，补齐对应资源。

### エスプガルーダII ブラックレーベル（espgaluda2）

```text
game-data/espgaluda2/
  default.xex
  dat00.xex
  dat11.xex
  ...
```

保留全部 `dat*.xex` 和原始资源目录，不要只提取 `default.xex`。`espgaluda2` 是完整游戏的数据位置，不是单独存放 Black Label DLC 的目录。

### 怒首領蜂最大往生（sdoj）

```text
game-data/sdoj/
  default.xex
  CA022100.bin
  CA022110.bin
  ...
```

使用受支持的原版光盘解包数据。当前未接入音乐补丁或其他程序更新，不要将 `default.xexp`、`CA022100.binp`、`CA022110.binp` 混入此目录；如已有混合版本，请从保留的原始本体重新提取一份。

本体内的金币、商店和游玩解锁内容按原作流程使用，不需要另找 DLC。

### ぐわんげ（guwange）

使用 Xbox 360 XBLA **Title ID 58410A78／本体 0.0.0.4**，不应用 Title Update。

```text
game-data/guwange/
  default.xex
  module/
    Guwange01_x360.dll
    Guwange01_coop_x360.dll
    Guwange_arrange_x360.dll
    Guwange_blue_x360.dll
  game_res/
  res/
  ...
```

保留全部 29 个文件及内部目录。这四个 DLL 是原始 Xbox 游戏模块，不是 Windows 程序，不要替换成运行程序目录里的同名组件。不要混入 `default.xexp` 或 `module/*.dllp` 更新文件；如果已有混合版本，从保留的本体重新提取到新目录。

## PS4 游戏

### ケツイ Deathtiny ～絆地獄たち～（ketsui）

支持 **CUSA09062／01.00 本体**和**本体覆盖 01.02 更新后的完整数据**，均使用一个游戏根目录：

```text
game-data/ketsui/
  eboot.bin
  ketsui_01000.prx
  ...
```

把本体目录**内部的全部内容**解压到 `game-data/ketsui/`，让 `eboot.bin` 直接位于其中，即可使用本体。要安装 01.02，将更新目录**内部的全部内容**覆盖到同一游戏根目录并替换同名文件；更新不是独立可玩的完整本体。

在启动器选择直接含 `eboot.bin` 的目录。扫描会显示实际识别的 01.00 或 01.02；未知或混合版本不能通过校验。

从旧双目录迁移时，在一个新目录先复制 `CUSA09062-app/` 内全部内容，再按需覆盖 `CUSA09062-patch/` 内全部内容，最后选择这个新目录。不要把两层目录名直接放进去；v0.4 不再使用旧双目录。保留原提取文件，程序不自动移动它们。想从 01.02 退回 01.00 时，重新提取完整本体到新目录，不要仅删除几个补丁文件。存档与录像仍位于原来的 `user-data/ketsui/`。

### 怒首領蜂 大往生 臨廻転生（daioujou）

支持 **CUSA32997／01.00 本体（离线）或 01.04 完整合并数据**。01.00 直接使用完整本体目录；社区排行榜和联网录像需要覆盖 01.04 更新。以下为更新后的结构：

```text
game-data/daioujou/
  eboot.bin
  daioujou_01000.prx
  daioujou_01010.prx
  ...
```

选择直接包含 `eboot.bin` 的目录，保留全部游戏模块和资源。单独的补丁目录不等于完整合并数据。

### エスプレイドΨ（esprade）

支持 **CUSA15155／01.00 本体覆盖 01.04 更新后的完整数据**：

```text
game-data/esprade/
  eboot.bin
  emu_integration_01500.prx
  esprade_01000.prx
  ps4data_body.bin
  ps4data_patch_body.bin
  sce_module/
  ...
```

将本体 PKG 提取到一个新目录，再将 01.04 更新中提取的全部内容覆盖到同一目录。把合并结果放进 `game-data/esprade/`，或在启动器直接选择这份合并目录。`eboot.bin` 应直接位于所选目录下，不要再套一层本体／更新目录。单独的更新包不能运行；保留其余模块、资源和内部层级。

PS4 作品首次启动时自动生成 `prepared/` 下的运行文件，原始游戏数据不变；这些生成物无须手工放进游戏数据目录。ESP Ra.De. 的存档、下载录像与设置位于 `user-data/esprade/`。

## 无法识别时怎么检查

**只有 ISO、PKG 或 ZIP 文件，能直接选吗？**

启动器接收已提取的文件夹，不直接装载 ISO／PKG。ZIP 如果装的是已经提取好的游戏文件，先完整解压，再选择实际游戏根目录；把压缩包或镜像文件放进 `game-data` 并不等于安装完成。

**文件夹一定要叫表里的名字吗？**

使用默认路径时按 `game-data/futari` 等固定位置放置。通过“选择数据目录…”指定外部位置时，最外层文件夹可自定义名称，支持中文和空格；里面的游戏文件名与相对层级须保留。

**文件都有了，为什么还提示缺少数据？**

先确认没有多套一层。例如实际入口在 `game-data/sdoj/解压目录/default.xex`，应选择这个“解压目录”，或将其中的完整内容放到 `game-data/sdoj/`。启动器可提示浅层候选位置，但不会自动改选；选择后仍须完成数据校验。也可检查启动器是否仍指向此前选过的外部路径。

**版本不支持，是改文件名就可以吗？**

不能。目录扫描检查所需文件及大小，启动前还会核对文件内容的 SHA-256。相同标题、相同文件名也可能对应不同版本；缺件应补齐对应版本，版本不符应使用支持的数据。开发者可在各 `games/<游戏 ID>/data.json` 中查看精确清单。

**更新程序时要重新放游戏吗？**

在原合集目录覆盖更新会继续使用原有游戏数据。若换了安装位置，可保留原数据目录并重新选择；配置、存档和录像整体转移 `user-data/`。旧版根目录的 `config.ini` 若尚未迁移，也需一并保留，启动器会自动迁入新位置。
