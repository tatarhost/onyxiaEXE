# OnyxiaEXE

A Windows 7 Aero-style desktop workbench for Roblox Studio projects.

## Features

- Searches running Windows processes for `Roblox` in the process name, executable path, or command line.
- Displays PID, process name, and RAM usage.
- Refreshes the process list automatically.
- Multi-tab Lua/Luau editor with basic syntax highlighting.
- Open, save, export, and copy scripts for use in Roblox Studio.
- Opens the local Roblox folder and Roblox Studio when installed.

## Run from source

Requires Python 3.10+ on Windows.

```powershell
py -m pip install -r requirements.txt
py main.py
```

## Build the EXE

The GitHub Actions workflow **Build Windows EXE** builds a standalone, windowed 64-bit executable on Windows.

It runs on pushes to `main` and can also be started manually from the Actions tab. Download the artifact named **OnyxiaEXE-Windows-x64** from the completed workflow run.

## Scope

Process inspection is read-only. The editor exports Lua/Luau source for Roblox Studio; it does not inject DLLs, modify Roblox process memory, or execute scripts inside the Roblox Player.
