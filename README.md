# NFS The Run - Wheels Force Feedback scaler

NFS The Run supports Direct Input wheels, but for some reason they completely forgot to let the user customize the force-feedback settings,
meaning it's stuck with an absurdly strong centering spring (in my opinion, at least with my G29).
This basically fixes it by intercepting the FFB parameters and letting the user custoimze them through the .ini provided.


## Prerequisites to compile


- **Visual Studio Build Tools 2019 or 2022**
  Download: https://visualstudio.microsoft.com/visual-cpp-build-tools/
  Select workload: **"Desktop development with C++"**



## Build

1. Open the **"x86 Native Tools Command Prompt for VS 20xx"**
   "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"

2. `cd` to this folder and run:
   ```
   build.bat
   ```

3. You should get `dinput8.dll` in the same folder.


## Install

Put `dinput8.dll` and `dinput8_proxy.ini` into the game folder where `NFS_Run.exe` is located.


## Tuning (dinput8_proxy.ini)

In `dinput8_proxy.ini` you will find the exposed settings with some instructions. 
The provided settings are what I find 'ok', but you can change it to whatever you like.