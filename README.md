# dinput8_proxy — NFS The Run FFB scaler

NFS The Run supports Direct Input wheels, but for some reason they completely forgot to let the user customize the force-feedback settings,
meaning it's stuck with an absurdly strong centering spring (in my opinion, at least with my G29).
This basically fixes it by intercepting the FFB parameters and letting the user custoimze them through the .ini provided.


---
## Prerequisites to compile


- **Visual Studio Build Tools 2019 or 2022**
  Download: https://visualstudio.microsoft.com/visual-cpp-build-tools/
  Select workload: **"Desktop development with C++"**

---

## Build

1. Open the **"x86 Native Tools Command Prompt for VS 20xx"**
   "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"

2. `cd` to this folder and run:
   ```
   build.bat
   ```

3. You should get `dinput8.dll` in the same folder.

---

## Install

Put `dinput8.dll` and `dinput8_proxy.ini` into the game folder where `NFS_Run.exe` is located.

---

## Tuning (dinput8_proxy.ini)

```ini
[FFB]
GainScale=0.30       ; master strength (0.0–1.0)
SpringScale=0.15     ; centering spring / damper — lower this to fix the crazy center force
ConstantScale=0.40   ; road feel, impacts
PeriodicScale=0.40   ; rumble strips, kerbs
```

Edit the INI while the game is closed, then restart. No recompile needed.

**Suggested starting point for G29:**
- `SpringScale=0.10` – `0.20` (centering is the main offender)
- `GainScale=0.25` – `0.35`
- `ConstantScale=0.35` – `0.50`
- `PeriodicScale=0.35` – `0.50`

