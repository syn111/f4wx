# F4Wx

```
  ______ _  ___          __     
 |  ____| || \ \        / /     
 | |__  | || |\ \  /\  / /_  __ 
 |  __| |__   _\ \/  \/ /\ \/ / 
 | |       | |  \  /\  /  >  <  
 |_|       |_|   \/  \/  /_/\_\ 
```

**F4Wx** is a real weather tool for BMS 4.36+ that lets you download and convert real weather (GRIB2) into the simulator in a user-friendly way.

For questions, bug reports, and feature suggestions, see the release thread on the BMS Community MODs/WIP forum.

---

## Credits and copyright

F4Wx is Copyright 2016–2026 **Ahmed**.

This repository includes third-party code. See [NOTICE](NOTICE) for a list of components and their licenses.

---

## Requirements

- Windows 7 or Windows Server 2008 R2 or later
- [Visual C++ Redistributable (x64)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) — the standard VC runtime (2015–2022 / latest supported)

---

## Installation (end users)

1. Extract all contents to a folder of your choice.
2. Run **F4Wx.exe**.

Generated `.fmap` files: single files go in your campaign directory (load manually in BMS); sequences go in the **WeatherMapsUpdates** directory inside the campaign folder and are loaded by BMS at the appropriate time.

See [f4wx/README.txt](src/f4wx/README.txt) for the full user guide, FAQ, and bug reporting.

---

## Building from source

1. Clone the repository (including submodules):
   ```bash
   git clone --recurse-submodules https://github.com/syn111/f4wx
   ```
   If you already cloned without submodules, run:
   ```bash
   git submodule update --init --recursive
   ```
2. Open **f4wx.sln** in Visual Studio 2017 or later.
3. Select the **f4wx** project and build (e.g. Release | x64).

The solution also includes:

- **g2c** – NCEPLIBS-g2c static library for GRIB2 decode (built automatically as a dependency of f4wx).

Dependencies (g2c) are Git submodules in `dependencies/`. GRIB2 support is provided by [NCEPLIBS-g2c](https://github.com/NOAA-EMC/NCEPLIBS-g2c) (built as a static lib with PNG/Jasper disabled for a single portable .exe).

---

## License

This project is licensed under the **Apache License 2.0**.  
See the [LICENSE](LICENSE) file for the full text.

You may use and reuse this code, including in proprietary projects, under the terms of that license. Attribution must be preserved as described in the license and in [NOTICE](NOTICE).

---

## Third-party code

This repository contains or links to third-party software. Copyright and license information for each component are listed in [NOTICE](NOTICE).
