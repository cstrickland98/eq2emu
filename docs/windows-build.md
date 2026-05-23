# Windows build and local run

This repository keeps the existing Linux makefiles and `linux_compile.sh` intact. The Windows path is additive and uses CMake, Ninja, MSVC, and vcpkg.

## Dependency inventory

The legacy Linux build expects MariaDB Connector/C, Boost, Lua, OpenSSL, zlib, fmt, glm, and Recast/Detour. It also assumes POSIX threads, POSIX sockets, `unistd.h`, Linux linker flags such as `-rdynamic`, `-ldl`, and hard-coded include/library roots such as `/usr/include/mariadb` and `/eq2emu/recastnavigation`.

The Windows CMake build replaces those paths and flags with imported packages from vcpkg:

- `Boost::filesystem`, `Boost::iostreams`, `Boost::program_options`, `Boost::regex`, `Boost::system`, `Boost::thread`
- `fmt::fmt`
- `glm::glm`
- `unofficial::lua::lua`, `unofficial::lua::lua-cpp`
- `OpenSSL::SSL`, `OpenSSL::Crypto`
- `RecastNavigation::Recast`, `RecastNavigation::Detour`, and related RecastNavigation targets
- `unofficial::libmariadb`
- `ZLIB::ZLIB`
- `ws2_32` on Windows

## Prerequisites

Install:

- Visual Studio 2022 with "Desktop development with C++"
- CMake 3.25 or newer
- Ninja
- vcpkg

Set `VCPKG_ROOT` to your vcpkg checkout, for example:

```powershell
$env:VCPKG_ROOT = "E:\_EQ2\vcpkg"
```

## Build

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-windows.ps1
```

The script imports the Visual Studio x64 build environment, configures the `windows-msvc-vcpkg` CMake preset, lets vcpkg install the manifest dependencies, and builds:

- `build\windows-msvc\bin\login.exe`
- `build\windows-msvc\bin\eq2world.exe`

To rebuild from a clean CMake directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Clean
```

## Prepare runtime files

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-windows.ps1
```

This copies the built executables and vcpkg runtime DLLs into `server\`, then creates these runtime files from examples when missing:

- `server\login_db.ini`
- `server\world_db.ini`
- `server\server_config.json`
- `server\log_config.xml`

It also copies MariaDB client authentication plugin DLLs and, when a sibling `..\eq2emu-content` checkout exists, creates junctions for content folders such as `Spells`, `Quests`, and `SpawnScripts`. For local standalone testing, `ENTERIP` placeholders in `server\server_config.json` are changed to `127.0.0.1`.

## Prepare local databases

The default server configs expect:

- login database: `eq2ls`
- world database: `eq2emu`
- database user/password: `root` / `pass`
- world-server account/password: `testlabs` / `testpass`

Use MariaDB for clean local runs because the world database dump uses MariaDB sequence syntax. MySQL 8.4 can launch the servers with compatibility flags, but imports the MariaDB sequence statements only when `mysql --force` is used and the world server logs sequence-related warnings.

With the database dump repository checked out next to this repository as `..\eq2emu-database`, provision a local database server with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup-windows-db.ps1 -StartLocalServer -ForceImport
```

For an already running MariaDB/MySQL server, omit `-StartLocalServer`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup-windows-db.ps1 -ForceImport
```

The setup script imports `eq2emu_login_db.tar.gz` and `eq2emu_world_db.tar.gz`, creates the `login_worldservers` row used by the default `server_config.json`, and stores the SHA-512 hash expected by the login server. Use `-DatabaseDir`, `-MysqlBinDir`, `-RootPassword`, `-WorldAccount`, or `-WorldPassword` if your local paths or credentials differ.

## Run locally

Start both servers from the `server\` working directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-windows.ps1 -Mode both -CheckPorts
```

`-CheckPorts` waits 45 seconds by default so the world server has time to finish loading before port checks run. Use `-PortCheckDelaySeconds` to adjust that delay.

Expected default binds:

- Login UDP port: `9100`
- World UDP port: `9001`

The world server also connects back to the login server using the `LoginServer` section of `server\server_config.json`. A complete local standalone run requires populated `eq2ls` and `eq2emu` databases matching `server\login_db.ini` and `server\world_db.ini`.

## Linux preservation

Do not remove or replace these existing Linux entry points:

- `linux_compile.sh`
- `source\LoginServer\makefile`
- `source\WorldServer\makefile`

The CMake build is a Windows-native path and does not rewrite the legacy Linux build flow.
