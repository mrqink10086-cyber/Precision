## Precision 2.0.6 + max-su V5

本地合并版：保留标准 2.0.6 修复和 V1–V4 API，合入影天分支的 V5、自定义命中载荷及 hitstop 重做。使用 CommonLibSSE-NG 15.7.3 构建，目标为 Skyrim 1.6.1170；MSL 1.2.2 可直接请求 V5。

安装时只覆盖原 Precision 的 DLL，保留原模组资源。合并细节、缓存返回值生命周期、验证范围和构建方式见 [docs/MERGE.md](docs/MERGE.md)。尚未进行游戏内测试。

Quite a bit of Havok-related code comes from [PLANCK](https://github.com/adamhynek/activeragdoll) and [HIGGS](https://github.com/adamhynek/higgs)

The debug drawing functionality is hastily put together from [SmoothCam](https://github.com/mwilsnd/SkyrimSE-SmoothCam)'s code that draws the arrow trajectory. I know it's horribly hacked together but it's just a debug feature, I didn't have time or a real reason to refactor it yet.
 

## Requirements
* [CMake](https://cmake.org/)
	* Add this to your `PATH`
* [The Elder Scrolls V: Skyrim Special Edition](https://store.steampowered.com/app/489830)
	* Add the environment variable `CompiledPluginsPath` to point to the folder where you want the .dll to be copied after building
* [Vcpkg](https://github.com/microsoft/vcpkg)
	* Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
* [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
	* Desktop development with C++
* [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
	* Add the environment variable `CommonLibSSEPath` with the value as the path to the folder containing CommonLibSSE-NG

## User Requirements
* [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Building
```
git clone https://github.com/ersh1/Precision/
cd Precision
git submodule init
git submodule update

cmake --preset vs2022-windows
cmake --build build --config Release
```

## License

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS). Specifically, the Modded Code is Skyrim (and its variants) and Modding Libraries include [SKSE](https://skse.silverlock.org/) and Commonlib (and variants).
