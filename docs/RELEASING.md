# 构建与发布

需要 Windows x64、PowerShell 7.2+、Visual Studio C++ Build Tools 和 Windows SDK。提取本机游戏界面资源另需 Python 与 `UnityPy==1.25.4`。

## 构建

在项目根目录运行：

```powershell
./build.ps1 -SkipGameCheck -PackageName dungeon-slasher-r27
```

如需同时检查已安装的游戏版本，将 `-SkipGameCheck` 替换为 `-GameDirectory '游戏安装目录'`。构建成功后，`dist` 文件夹中会生成 ZIP 和同名 SHA-256 校验文件。

## 发布

1. 更新功能和对应使用说明，将功能变化统一写入根目录的 `CHANGELOG.md`，不另建版本日志。
2. 确认构建、检查和分发包内容通过，且游戏版本与 README 中声明的适配版本一致。
3. 提交并推送代码，为未发布过的 `r<number>` 创建草稿 Release，上传本机已验证的 ZIP 和 SHA-256，填写本次版本的更新说明。
4. 创建并推送同名标签。GitHub Actions 会构建并运行回归测试，再核验草稿附件的校验值、文件清单和原生美术资源，最后将草稿正式发布。CI 生成的备用样式包不会覆盖本机附件。
5. 发布完成后下载附件，核对内容和校验值。

普通 CI 在推送主分支、提交 PR 或手动触发时运行。CI 不包含游戏文件，构建通过不代表已验证实际游戏表现。

## 分发内容

ZIP 包含启动器、运行模块、使用说明，以及项目、MinHook、Dear ImGui 的三份许可证。本机构建另含从已安装游戏提取的 `native-ui.bin`；源码和无缓存的 CI 构建不包含游戏美术资源。个人配置、存档、备份和游戏文件不进入分发包。
