# God for DUNGEON SLASHER

《DUNGEON SLASHER》Windows Steam 版中文修改器。当前版本 **r26**，适配 **Steam Build 26725751**。

[下载最新版本](https://github.com/2683445453/God-for-Dungeon-slasher/releases/latest) · [r26 发布说明](https://github.com/2683445453/God-for-Dungeon-slasher/releases/tag/r26)

启动游戏进入主菜单后运行 `DungeonSlasherTrainer.exe`，启动器完成校验、备份和注入后自动退出。功能和设置保存在游戏内，面板复用游戏原生字体、灰色／黄色按钮、像素边框和勾选图案，使用左侧分类与右侧功能列表，并随游戏分辨率调整字号和尺寸。

**F2** 显示或隐藏面板，**F8** 关闭全部功能。再次运行启动器会打开已有面板；游戏重启后重新运行启动器。数值输入后回车应用。

更新前完全退出游戏和旧修改器。完整解压，保持 EXE、DLL 和 `native-ui.bin` 在一起。设置保存在同目录 `trainer.ini`，存档备份位于 `backups`。扩容存档请先注入再继续；恢复原有容量前先移走超额物品。

## 功能

- 战斗：无敌；伤害倍率 0.1–1000；普通技能、增益技能和武器技能无冷却。
- 移动：无限冲刺、无限跳跃。
- 收益：金币、结算奖励、符文粉末、钻石独立倍率 0.1–1000。
- 容量：圣器装备、普通技能装备、圣器背包、技能背包分别调整 6–50 格；增益技能扩展至 6 格。
- 解锁：角色、角色皮肤与大厅背景、武器、宠物；一键解锁全部可用技能和圣器。
- 罪恶：总点数 0–140，七项各最多 20 点。
- 符文：重铸、精炼最高值；普通怪和首领掉落概率 0–100%；掉落数量 1–20 倍。
- 货币：42 项货币目标余额编辑，支持负余额修复。
- 连接：连接失败时自动重试。
- 设置：自动保存开关和参数。

## 从源码构建

需要 Windows x64、PowerShell 7.2+、Visual Studio C++ Build Tools、Windows SDK 和 Python。原生资源提取使用 `UnityPy==1.25.4`。

```powershell
python -m pip install UnityPy==1.25.4
./build.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\DUNGEON SLASHER'
```

生成文件位于 `dist/dungeon-slasher-r26`，压缩包为 `dist/dungeon-slasher-r26.zip`。构建时从本机安装的游戏中提取原生界面资源到 `native-ui.bin`；资源不进入源码。源码构建可使用 `-SkipGameCheck`，没有本地资源缓存时使用备用样式。

[更新日志](CHANGELOG.md) · [功能说明](docs/使用说明.txt) · [发布指南](docs/RELEASING.md)

## 许可证

项目采用 [MIT License](LICENSE)。[MinHook](third_party/minhook/LICENSE.txt) 与 [Dear ImGui](third_party/imgui/LICENSE.txt) 保留各自许可证。游戏名称和素材属于各自权利人，源码不包含游戏本体或美术资源。
