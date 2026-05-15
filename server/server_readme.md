# 📦 EQ2Emu Server Directory Layout

> This document describes the structure and purpose of the EQ2Emu server directory.

## 📚 Table of Contents

- [Overview](#overview)
- [Network Structure XML Files](#network-structure-xml-files)
- [Script Folders](#script-folders)
- [Data Folders](#data-folders)
- [Definition Files](#definition-files)
- [Executables](#executables)
- [Example Configuration Files](#example-configuration-files)
- [Notes](#notes)

---

## Overview

```
/ (base directory)
├── *.xml (Network Structure Files)
├── ItemScripts/
├── Quests/
├── RegionScripts/
├── SpawnScripts/
├── Spells/
├── ZoneScripts/
├── Maps/
├── Regions/
├── logs/
├── log_config.xml
├── login_db.ini
├── world_db.ini
├── server_config.json
├── login (executable)
└── eq2world (executable)
```

---

<details>
<summary><strong>📡 Network Structure XML Files</strong></summary>

These XML files define the client-server network structures used by EQ2Emu:

- `CommonStructs.xml`
- `EQ2_Structs.xml`
- `ItemStructs.xml`
- `LoginStructs.xml`
- `SpawnStructs.xml`
- `WorldStructs.xml`

</details>

---

<details>
<summary><strong>🧹 Script Folders</strong></summary>

Custom server-side Lua scripts that drive server content:

- `ItemScripts/`: Item-specific scripts.
- `Quests/`: Quest-related scripts.
- `RegionScripts/`: Regional behavior.
- `SpawnScripts/`: NPC spawning behavior.
- `Spells/`: Spell logic.
- `ZoneScripts/`: Zone triggers and scripting.

</details>

---

<details>
<summary><strong>📂 Data Folders</strong></summary>

Stores important data and logs:

- `Maps/`: Zone layout and navigation meshes.
- `Regions/`: Zone regional metadata.
- `logs/`: Runtime logs for diagnostics.

</details>

---

<details>
<summary><strong>⚙️ Definition Files</strong></summary>

Configuration files necessary to operate the server:

- [`log_config.xml`](https://github.com/emagi/eq2emu/blob/main/server/log_config.xml.example): Logging configuration.
- [`login_db.ini`](https://github.com/emagi/eq2emu/blob/main/server/login_db.ini.example): Login server database connection.
- [`world_db.ini`](https://github.com/emagi/eq2emu/blob/main/server/world_db.ini.example): World server database connection.
- [`server_config.json`](https://github.com/emagi/eq2emu/blob/main/server/server_config.json.example): Main configuration for login/world server and ports.

</details>

---

<details>
<summary><strong>🚀 Executables</strong></summary>

### `login`

- Handles login from EverQuest II clients.
- Clients use `cl_ls_address <host>` in `eq2_default.ini` to connect when the
  login server is on the default port, for example `cl_ls_address 127.0.0.1`.
- Default UDP Port: `9100`.
- Configured via `LoginConfig -> ServerPort` in [`server_config.json`](https://github.com/emagi/eq2emu/blob/main/server/server_config.json.example).

### `eq2world`

- Acts as the world and zone server.
- Connects to `login` via TCP on `9100` (default).
- Manages character creation, server list, and zoning.
- Default UDP port: `9001` (can be customized via `LoginServer -> worldport` in [`server_config.json`](https://github.com/emagi/eq2emu/blob/main/server/server_config.json.example)).

</details>

---

<details>
<summary><strong>📑 Example Configuration Files</strong></summary>

These examples are included for quick setup:

- [`log_config.xml.example`](https://github.com/emagi/eq2emu/blob/main/server/log_config.xml.example)
- [`login_db.ini.example`](https://github.com/emagi/eq2emu/blob/main/server/login_db.ini.example)
- [`world_db.ini.example`](https://github.com/emagi/eq2emu/blob/main/server/world_db.ini.example)
- [`server_config.json.example`](https://github.com/emagi/eq2emu/blob/main/server/server_config.json.example)

</details>

---

## 📌 Notes

- All ports can be customized via `server_config.json`.
- Lua script folders are modular and extensible.
- Example config files are included to assist with initial setup.
