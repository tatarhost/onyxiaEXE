# OnyxiaEXE — Native C++ Edition

A native Windows desktop utility styled after the Windows 7 Aero era.

## Features

- Searches Windows process executable names and resolved executable paths for Roblox.
- Displays PID, process name, working-set RAM, and the selected executable path.
- Automatically refreshes the process list.
- Multi-tab Lua/Luau editor with basic syntax colouring.
- Open, save, export, and copy Luau source for Roblox Studio.
- **Analyze Luau** runs `luau-analyze.exe` locally when it is available on PATH.
- **Run Local** runs `luau.exe` locally when it is available on PATH; execution has a 15-second timeout and output is captured in the app.
- Opens Roblox Studio when installed in the standard local app-data folder.
- Native Windows executable; no Python runtime or Python packages.

## Run the Luau tools

Install the official Luau CLI tools separately and make sure `luau.exe` and `luau-analyze.exe` are available on PATH. Restart OnyxiaEXE after changing PATH.

`Run Local` executes ordinary Luau outside Roblox. Roblox globals such as `game`, `workspace`, and `Players.LocalPlayer` are provided by Roblox Studio and are not available in the standalone CLI.

## Build locally

Install Visual Studio 2022 Build Tools with the **Desktop development with C++** workload and CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

Output: `build/Release/OnyxiaEXE.exe`.

## GitHub Actions

Every push to `main` builds a standalone x64 executable. Download artifact `OnyxiaEXE-Cpp-Windows-x64` from the completed **Build Native Windows EXE** workflow.

## Runtime scope

Process inspection is read-only. Luau source is edited, analysed, and run locally, or exported for use in Roblox Studio. This project does not inject DLLs into Roblox Player, modify its memory, or execute arbitrary code inside the game client.
