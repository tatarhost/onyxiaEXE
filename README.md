# OnyxiaEXE — Native C++ Edition

A native Windows desktop utility styled after the Windows 7 Aero era.

## Stack
- C++20
- Win32 API and native Windows controls
- Common Controls / Rich Edit
- CMake + MSVC

## Features
- Searches process executable names and resolved executable paths for Roblox.
- Displays PID, executable name, working-set RAM, and selected executable path.
- Automatically refreshes the process list.
- Multi-tab Lua/Luau editor with basic syntax colouring.
- Open, save, export, and copy Luau source for Roblox Studio.
- Opens Roblox Studio when installed in the standard local app-data folder.
- Native Windows executable; no Python runtime or Python packages.

## Build locally
Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload and CMake.

    cmake -S . -B build -A x64
    cmake --build build --config Release --parallel

Output: build/Release/OnyxiaEXE.exe

## GitHub Actions
Every push to main builds a standalone x64 executable. Download artifact OnyxiaEXE-Cpp-Windows-x64 from the completed Build Native Windows EXE workflow.

## Runtime scope
Process inspection is read-only. Luau source is edited and exported for use in Roblox Studio. This project does not inject DLLs into Roblox Player, modify its memory, or execute arbitrary code inside the game client.
