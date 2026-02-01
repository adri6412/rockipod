# RockIpod (Rockbox Hard Fork)

> **⚠️ DISCLAIMER: PERSONAL PROJECT**
> This project is a personal fork of Rockbox. It is provided **"AS IS"** without support or warranty of any kind. Future maintenance or updates are not guaranteed.

## Supported Devices
This fork has been **tested EXCLUSIVELY on the FiiO M3K**.
While the underlying code is based on Rockbox, modifications made for this specific "iPod-like" experience may not be compatible with or stable on other targets. **Use at your own risk.**

## Features
*   **iPod-like UI**: A custom interface designed to mimic the classic iPod experience.
*   **Custom Database**: Specialized database structure for optimized browsing.
*   **PC Companion App**: A dedicated Electron-based Desktop Music Manager (in `pc_app/`) to manage your library, parse metadata, and sync with the device.
*   **Custom Boot Logo**: "RockIpod" branded boot splash.

## Compilation Guide (Windows + WSL)
To compile this project for the FiiO M3K, a helper script is provided to automate the build process using the Windows Subsystem for Linux (WSL).

### Prerequisites
1.  **WSL Enabled**: You must have WSL installed (e.g., Ubuntu).
2.  **Rockbox Toolchain**: **CRITICAL**: Do NOT rely on `apt-get install gcc-mipsel-linux-gnu`. You **must** build the official toolchain using the helper script inside `tools/`.
    *   Enter WSL: `wsl`
    *   Navigate to the tools directory: `cd tools`
    *   Run the build script: `sudo ./rockboxdev.sh --target=i`
    *   Follow the prompts. This ensures you have the correct `mipsel-elf-gcc` compiler.

### How to Build
1.  Open a terminal (Command Prompt or PowerShell) in the root of this repository.
2.  Run the provided batch script:
    ```cmd
    .\build_m3k_wsl.bat
    ```
3.  The script will invoke the build process inside WSL and produce the output files.

## PC Music Manager
The desktop application is located in the `pc_app` directory.

### Quick Start
```bash
cd pc_app
npm install
npm start
```

---
*Based on [Rockbox](https://www.rockbox.org/) source code (GPL).*
