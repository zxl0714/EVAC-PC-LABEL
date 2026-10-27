PS4 PKG extraction / PS4 PKG 解包

双击同目录的 Extract-PS4.exe，选择 PKG 文件和新输出目录，点击“Extract”。
完成后点击“Open game files”。本体与更新分别解包：先将本体的全部游戏文件
复制到 game-data/<游戏>/，再用更新的全部游戏文件覆盖同名文件。
游戏目录中直接放 eboot.bin、sce_sys 等，不要再套一层 uroot 文件夹。
PkgTool 及依赖位于 pkgtool/，保留整个 tools/ 目录即可。
解包失败时查看输出目录中的 extract.log。原始 PKG 不会改动。

Double-click Extract-PS4.exe, choose a PKG and a new output folder, then click
Extract. When finished, click Open game files. Extract the base and update
separately: copy all base-game files into game-data/<game>/, then overlay all
update files, replacing matching files and keeping the rest of the base game.
Place eboot.bin and sce_sys directly in the game folder, without an extra uroot
folder. Keep the entire tools/ folder; pkgtool/ contains the required dependency.
If extraction fails, check extract.log in the output folder. Input PKGs are unchanged.

Game / 游戏          Base / 本体      Update / 更新
ketsui               01.00            01.02 optional / 可选
daioujou             01.00            01.04
esprade              01.00            01.04 required / 必需

The default PKG passcode is 32 zeros. Change it only if your PKG uses another
passcode. A ZIP/RAR archive password is separate from the PKG passcode.
PKG passcode 默认是 32 个零，仅在 PKG 使用其他 passcode 时修改；它与 ZIP/RAR
的解压密码不同。
