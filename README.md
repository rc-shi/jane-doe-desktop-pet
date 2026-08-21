# 简杜桌宠

这是一个 Windows 桌宠项目，包含可直接运行的程序、源码和动作帧素材。

## 直接使用

双击运行 `pet.exe` 即可启动桌宠。

运行时需要同目录下的 `jane_frames/` 文件夹，里面是桌宠动作帧素材。请不要把 `pet.exe` 单独拿走运行。

## 修改源码

`pet.cpp` 是桌宠源码。要修改动作逻辑、尺寸、点击触发、待机行为等功能，需要编辑这个文件，然后重新编译生成 `pet.exe`。

使用 MinGW g++ 可以这样编译：

```powershell
g++ pet.cpp -mwindows -O2 -std=c++17 -lgdiplus -lgdi32 -luser32 -lkernel32 -o pet.exe
```

## 文件

- `pet.cpp`：桌宠源码
- `pet.exe`：已经编译好的 Windows 可执行程序
- `jane_frames/`：动作帧素材
