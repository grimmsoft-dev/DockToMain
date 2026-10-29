# Dock To Main Window

An Unreal Engine 5 editor plugin that keeps the editor in one window. Tabs that would open as a separate floating window, such as Output Log, Project Settings, Widget Reflector, tool panels, or an editor's extra Details and Find Results panels, are docked into the main editor window instead.

- Asset editors open as tabs in the main window. The plugin turns on the engine's own **Editor Preferences → Asset Editor Open Location → Main Window**.
- Global (nomad) tabs are docked beside the Level Editor tab.
- An editor's panel tabs join another tab of the same editor that is already in the main window.
- Tabs you drag out on purpose stay floating.

Turn it off for a session with the console command `DockToMain.Enabled 0`; `DockToMain.Enabled 1` turns it back on. Each moved tab is logged under `LogDockToMain`.

The plugin is editor-only: it is never included in packaged games.

> Slate has no official setting for this, so the plugin moves tabs using engine internals. If a particular panel misbehaves after being docked, please open an issue naming the panel.

## Install

1. Download `DockToMain-<version>-Installer.zip` from [Releases](../../releases) and unzip it.
2. Run `Install.bat`.
3. Tick the engines to install into (every project using that engine gets the plugin), or pick a single `.uproject`. If an engine isn't listed (moved, copied or source-built), click **Add engine folder...** and choose its folder.
4. Click **Install**, then restart Unreal Editor.

If an engine is installed under `Program Files`, right-click `Install.bat` and choose **Run as administrator**.

From a console:

```bat
Install.bat -Engine 5.6
Install.bat -Engine "D:\Engines\UE_5.6"
Install.bat -Project "D:\MyGame\MyGame.uproject"
Install.bat -Engine 5.6 -Uninstall
```

Engine installs go to `<Engine>\Engine\Plugins\Marketplace\DockToMain`; project installs go to `<Project>\Plugins\DockToMain`.

## Compatibility

Supports UE 5.0 and later. The installer includes prebuilt binaries for the engine versions listed in each release. For any other 5.x version, install into a C++ project and the editor compiles the plugin when the project opens.

## Building from source

```powershell
.\Scripts\Build.ps1                  # every UE5 engine installed on this machine
.\Scripts\Build.ps1 -Engines 5.4,5.6 # only these
```

The script builds the plugin for each engine into `Build\UE<ver>\DockToMain` (logs in `Build\Logs`) and packages `Dist\DockToMain-<version>-Installer.zip`. It needs Visual Studio 2022 with the C++ workload. UE 5.0–5.3 also need the MSVC 14.36 toolchain ("MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.36)" in the Visual Studio Installer), which the script selects automatically.

## Releasing

1. Bump `Version` and `VersionName` in `DockToMain/DockToMain.uplugin` and add a line to `CHANGELOG.md`.
2. Run `.\Scripts\Build.ps1`.
3. Tag and upload:

```powershell
git tag v1.0.0; git push --tags
gh release create v1.0.0 Dist\DockToMain-1.0.0-Installer.zip --notes-file CHANGELOG.md
```
