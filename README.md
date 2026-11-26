# ENSC 351 — Autonomous Photo Booth

## 1. Project overview

This project implements an autonomous photo booth on the BeagleY-AI (BYAI). The Linux application handles camera capture, HDMI preview, ambient light sensing and coordination. A Zephyr application running on the MCU R5F core controls the NeoPixel countdown and flash independently of Linux scheduling.

This guide is intended for teammates starting from a fresh development environment. Commands are labelled **HOST** (Debian VM) or **TARGET** (BeagleY-AI via SSH). Do not run host compilation commands on the target. Commands assume the existing course Debian environment and the `vboxuser` account; substitute your own paths and network addresses if necessary.

### Implementation status

| Component | Status |
|---|---|
| HDMI live preview | Working |
| Ambient light ADC and flash decision | Working |
| R5 NeoPixel countdown and cancellation | Working |
| Linux-to-R5 pulse interface | Working |
| Single photo and ten-photo burst | Working |
| Flash synchronization | Initial testing successful; 2500 ms latest configuration |
| Preview restart and framebuffer cleanup | Working |
| R5 overlap protection | Implemented; targeted stress testing recommended |
| Startup script | Working in existing development setup |
| Physical capture/cancel buttons | Outstanding group integration |
| Audio countdown | Outstanding group integration |
| Website, image conversion and retrieval | Outstanding group integration |

The remaining group components should reuse the existing capture and countdown pipeline. They are not prerequisites for testing the current implementation.

## 2. System architecture

```text
Light sensor -> MCP3208 -> SPI -> Linux application
                                      |
Keyboard / future buttons / web ------+
                                      |
                             GPIO command pulse
                                      |
                                      v
                               MCU R5F / Zephyr
                                      |
                                NeoPixel ring

USB webcam -> FFmpeg -> /dev/fb0 -> HDMI display (preview)
     |
     +------> V4L2 grabber -> PPM files (capture)
```

Linux and R5 run concurrently. Linux owns the camera; the R5 owns the NeoPixel data output. The camera cannot be opened simultaneously by the current preview and capture implementations. Linux therefore stops FFmpeg, waits for it to exit, opens the camera through the grabber, saves the requested photos, releases the camera and restores the preview.

### Repository layout

Known paths and components (inspect the actual checkout for additional group files):

```text
ENSC351-Autonomous-Photobooth/
  CMakeLists.txt
  app/                         Linux application (locate main.c here)
  hal/                         Hardware abstraction layer
  photoGrabber/grabber.c       V4L2 capture implementation
  photoGrabber/                Grabber header/build configuration
  r5/                          Zephyr application source
  scripts/start_photobooth.sh  Target startup script
  build-arm64/                 Generated Linux build directory
  build-r5/                    Generated Zephyr build directory
```

Do not commit generated build directories, ELF binaries or captured photos unless the team explicitly agrees to distribute a release artifact. Use `git status` before committing.

## 3. Hardware and prerequisites

- BeagleY-AI running the course Debian image (existing setup uses Debian Bookworm).
- Debian 12 host VM with course cross-compilation tools and USB networking.
- Logitech C270 USB webcam; current device `/dev/video3`.
- HDMI display configured for 1024 x 600, framebuffer `/dev/fb0`.
- MCP3208 ADC on SPI0 (`/dev/spidev0.0`), channel 0, 3.3 V reference, photoresistor divider.
- 24-pixel WS2812 NeoPixel ring, 74HCT244 level shifting, common ground.
- Existing GPIO trigger: Linux `gpiochip0`, line 10 (application labels this GPIO24); R5 reads bit 10. R5 NeoPixel output uses bit 7. These are controller offsets, not physical header pin numbers. Confirm pinmux and wiring against the team wiring diagram before changing connections.

The complete wiring diagram is not reproduced here because the final physical pin mapping has not been independently verified. Do not infer physical pin numbers from GPIO offsets. Power down before rewiring. The ring is currently used at reduced brightness; do not increase brightness or assume the board can safely supply full-white current for 24 LEDs. Verify power budget and use a suitable external regulated supply with common ground if needed.

## 4. Host setup and repository

### 4.1 Install basic tools (HOST)

```bash
sudo apt update
sudo apt install git cmake ninja-build build-essential gcc-aarch64-linux-gnu \
  g++-aarch64-linux-gnu python3 python3-venv python3-pip python3-dev \
  device-tree-compiler gperf wget xz-utils file nfs-kernel-server
```

Use the course-provided cross-compilation sysroot/toolchain where available. Installing the compiler alone does not supply ARM64 libgpiod and libv4l2 development libraries. The existing application uses the libgpiod v1 API, so a v2-only development environment is not a drop-in replacement.

### 4.2 Clone or update (HOST)

```bash
mkdir -p ~/ensc351/work ~/ensc351/public
cd ~/ensc351/work
# Replace <repository-url> with the actual team Git remote:
git clone <repository-url> ENSC351-Autonomous-Photobooth
cd ENSC351-Autonomous-Photobooth
```

If already cloned, use `git status`, then `git pull --ff-only` when local changes are safely committed. Do not clone over an existing checkout. Verify files with:

```bash
find . -maxdepth 3 \( -name CMakeLists.txt -o -name main.c -o -name grabber.h -o -name prj.conf -o -name '*toolchain*' \)
git remote -v
```

## 5. USB network and NFS deployment

The working setup uses host `192.168.7.1`, target `192.168.7.2`, and exports `/home/vboxuser/ensc351/public` to `/mnt/remote`. Confirm these addresses; USB interfaces can differ between machines.

### 5.1 Check connectivity

HOST and TARGET:

```bash
ip -br addr
```

HOST: `ping -c 3 192.168.7.2`

TARGET: `ping -c 3 192.168.7.1`

If using VirtualBox, attach the Linux Foundation multifunction USB gadget to the Debian VM through Devices > USB. If the host USB network interface has a different subnet, configure it using the course networking guide; do not blindly change an unrelated interface.

### 5.2 Configure NFS if not already set up (HOST)

```bash
mkdir -p ~/ensc351/public
sudoedit /etc/exports
```

Example export for this isolated development link (replace path/address if different):

```text
/home/vboxuser/ensc351/public 192.168.7.2(rw,sync,no_subtree_check)
```

```bash
sudo exportfs -ra
sudo systemctl enable --now nfs-kernel-server
sudo exportfs -v
```

Keep default root squashing; do not make the export world-writable. The normal host and target users need appropriate permissions for copying test photos. The root-run application reads deployed files from NFS but saves photos locally on the target.

### 5.3 Mount and verify (TARGET)

```bash
sudo apt install nfs-common
sudo mkdir -p /mnt/remote
mountpoint -q /mnt/remote || sudo mount -t nfs 192.168.7.1:/home/vboxuser/ensc351/public /mnt/remote
ls -la /mnt/remote
```

Create a test file as the host user in `~/ensc351/public` and confirm it appears on the target. The startup script also mounts NFS when necessary.

## 6. Linux application compilation

### 6.1 Existing configured build (HOST)

```bash
cd ~/ensc351/work/ENSC351-Autonomous-Photobooth
cmake --build build-arm64 -j$(nproc)
```

The existing course CMake deployment is expected to produce `/home/vboxuser/ensc351/public/myApps/hello_world`. Check after every build:

```bash
ls -l ~/ensc351/public/myApps/hello_world
file ~/ensc351/public/myApps/hello_world
```

It must be an ARM aarch64 executable, not x86-64.

### 6.2 Fresh build directory (HOST)

First inspect the root CMakeLists.txt and any toolchain files/presets. The exact original configure command and sysroot are not available in this handover; use the course's ARM64 configuration, not a native host build.

```bash
cmake --list-presets
find . -iname '*toolchain*' -o -name CMakePresets.json
```

If the repository supplies a toolchain file, the usual configure pattern is:

```bash
# Replace the placeholder with the actual course toolchain file.
cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=/path/to/arm64-toolchain.cmake
cmake --build build-arm64 -j$(nproc)
```

If the course project sets the compiler itself, follow that configuration instead. Do not reuse another person's CMakeCache.txt: it contains absolute paths. Ensure ARM64 libgpiod (v1), libv4l2 and pthread dependencies resolve against the target-compatible sysroot. Do not link host x86 libraries into the target executable.

If automatic deployment is not configured, locate the built executable and copy it:

```bash
find build-arm64 -type f -name hello_world
mkdir -p ~/ensc351/public/myApps
# Replace the source path with the result of find.
cp /path/to/built/hello_world ~/ensc351/public/myApps/hello_world
```

### 6.3 Target runtime dependencies (TARGET)

```bash
sudo apt install ffmpeg v4l-utils libv4l-0 libgpiod2
ls -l /dev/video* /dev/fb0 /dev/spidev* /dev/gpiochip*
v4l2-ctl --list-devices
ldd /mnt/remote/myApps/hello_world
```

The Debian package names above assume Bookworm. Check `ldd` for missing libraries. Root is currently used for hardware access; do not solve permission problems by globally making hardware devices writable.

## 7. Zephyr and R5 firmware — first-time setup

### 7.1 What Zephyr is

Zephyr is the RTOS used on the MCU R5F, not the Linux OS running on the main processor. `west` manages the Zephyr workspace and builds the firmware. The SDK supplies the embedded cross-compiler. Linux's aarch64 compiler is not the R5 compiler.

The existing setup uses `~/zephyrproject/zephyr`, `~/zephyr-venv`, and west 1.5.0. Allow substantial disk space (roughly 30 GB free is a sensible starting point). The exact Zephyr revision and SDK version of the working installation are not recorded in the supplied source. Obtain them from a working machine before initializing a fresh workspace:

```bash
source ~/zephyr-venv/bin/activate
cd ~/zephyrproject/zephyr
west --version
git rev-parse HEAD
git describe --tags --always
west topdir
ls -d ~/zephyr-sdk* /opt/zephyr-sdk* 2>/dev/null
```

Share the revision and SDK version with the team. Do not assume the newest Zephyr release supports the same board definitions or APIs.

### 7.2 Create Python environment (HOST)

```bash
python3 -m venv ~/zephyr-venv
source ~/zephyr-venv/bin/activate
python -m pip install --upgrade pip
python -m pip install west==1.5.0
west --version
```

Activate this environment in each new terminal before running west.

### 7.3 Initialize workspace (HOST, once only)

```bash
# Replace <working-zephyr-revision> with the verified revision from 7.1.
west init -m https://github.com/zephyrproject-rtos/zephyr --mr <working-zephyr-revision> ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
```

If the workspace already exists, skip `west init`. If the course uses a custom manifest or patched board files, use the course repository instead of the upstream URL. A fresh upstream checkout is not guaranteed to reproduce a course-patched environment.

Install Python dependencies using the method supported by that revision:

```bash
cd ~/zephyrproject/zephyr
west packages pip --install
```

For older revisions without that command, use the requirements file if present:

```bash
python -m pip install -r scripts/requirements.txt
```

If `west update` fails for a module, retry after checking network and disk space. Do not delete the workspace unnecessarily.

### 7.4 Install Zephyr SDK (HOST, once only)

Use the SDK version compatible with the verified Zephyr revision. For revisions supporting the SDK command:

```bash
cd ~/zephyrproject/zephyr
west sdk install --help
west sdk install
```

If unavailable, follow the SDK installation section of the documentation for the pinned Zephyr version: https://docs.zephyrproject.org/latest/develop/getting_started/index.html . Select the matching version of the docs and SDK rather than blindly using latest. For manual SDK installation, run the downloaded SDK's setup script and select the ARM embedded toolchain. If required, export `ZEPHYR_SDK_INSTALL_DIR` to the actual installed SDK directory. Do not point it at the Linux aarch64 compiler.

### 7.5 Confirm board support

```bash
cd ~/zephyrproject/zephyr
west topdir
west boards | grep -i beagley
```

The working target is `beagley_ai/j722s/mcu_r5f0_0`. Do not select the main R5F core or a different board. If the target is missing, verify the Zephyr revision and course board patches before continuing.

## 8. Compile and deploy R5 firmware

### 8.1 First build / pristine build (HOST)

```bash
source ~/zephyr-venv/bin/activate
cd ~/zephyrproject/zephyr
west build -p always -b beagley_ai/j722s/mcu_r5f0_0 \
  ~/ensc351/work/ENSC351-Autonomous-Photobooth/r5 \
  -d ~/ensc351/work/ENSC351-Autonomous-Photobooth/build-r5
```

`-b` selects the board/core; the application path selects the project; `-d` selects the output directory; `-p always` clears generated build state. This does not delete the source. The build should create `build-r5/zephyr/zephyr.elf`.

### 8.2 Subsequent builds (HOST)

```bash
source ~/zephyr-venv/bin/activate
cd ~/zephyrproject/zephyr
west build -d ~/ensc351/work/ENSC351-Autonomous-Photobooth/build-r5
```

Run from inside the Zephyr workspace even though the application is in a separate Git repository. Running west from the application repository previously resulted in an unknown build command.

### 8.3 Copy firmware to NFS (HOST)

```bash
cp ~/ensc351/work/ENSC351-Autonomous-Photobooth/build-r5/zephyr/zephyr.elf \
   ~/ensc351/public/photobooth-r5-cancel.elf
file ~/ensc351/public/photobooth-r5-cancel.elf
```

The ELF should be a 32-bit ARM firmware image, not the aarch64 Linux executable. Stop the running photo booth before replacing deployed files or restarting firmware.

## 9. Startup script and operation

### 9.1 Install script (HOST then TARGET)

HOST:

```bash
cd ~/ensc351/work/ENSC351-Autonomous-Photobooth
cp scripts/start_photobooth.sh ~/ensc351/public/
```

TARGET (mount NFS first if necessary):

```bash
sudo install -m 755 /mnt/remote/start_photobooth.sh /usr/local/bin/start_photobooth
sudo start_photobooth
```

Reinstall the script only when it changes or on a new target. The installed script is a copy, not a live reference to the repository.

The existing script is intended to find the MCU R5F by name (`79000000.r5f`), mount NFS, check deployed artifacts, compare the firmware with the installed copy, restart the core if necessary, enable HDMI, hide the console cursor, create the photo directory and execute the Linux application. Inspect the actual script if any step fails. It does not own the FFmpeg preview; Linux main does.

### 9.2 Inspect remote processors (TARGET)

```bash
for r in /sys/class/remoteproc/remoteproc*; do
    echo "$r"
    cat "$r/name" "$r/state" "$r/firmware"
    echo
done
```

The MCU R5F was `remoteproc2` on the existing board; numbering can change. Identify by name, not number. Do not stop unrelated cores. Prefer the startup script over manual remoteproc commands. If a stop returns `Device or resource busy`, inspect `sudo dmesg | tail -n 80` and the course R5 instructions; do not force a different core to stop. UART console output requires appropriate hardware and is not necessary for normal operation.

### 9.3 Controls

| Input | Action |
|---|---|
| Enter | Three-second countdown, one photo |
| B then Enter | Three-second countdown, ten photos |
| C then Enter | Cancel active countdown |
| P then Enter | Immediate single test capture, no new R5 countdown |
| Ctrl+C | Stop application and preview, release hardware, clear display |

Cancellation is supported during countdown, not while the blocking capture operation is saving frames. The keyboard interface currently expects an interactive terminal. A headless service or website must not assume stdin is always available.

## 10. Timing and interface contract

| Parameter | Value |
|---|---|
| Ambient flash threshold | 0.20 V (flash if below threshold) |
| Cancel pulse | 50 ms |
| Countdown without flash pulse | 200 ms |
| Countdown with flash pulse | 800 ms |
| Countdown duration | 3000 ms after command pulse |
| Flash duration | 2500 ms latest tested setting |
| R5 busy margin | 100 ms |
| Linux keyboard poll | 20 ms |
| Burst count | 10 |
| Camera warmup | 20 frames, final warmup frame saved |

The R5 interprets pulse widths, then starts the countdown after the pulse ends. The ring displays green, yellow and red countdown stages, followed by white flash when requested. Linux uses monotonic time to schedule capture and prevent overlapping sequences. There is no R5 completion acknowledgement in the current design. The guard is time-based, not a hardware handshake.

**Keep `FLASH_DURATION_MS` synchronized in both Linux and R5 sources.** The previously shared main source had 2000 ms; the latest reported working setting is 2500 ms. Check the actual repository before building:

```bash
grep -R -n 'define FLASH_DURATION_MS' app r5
```

When changing this constant, rebuild and deploy both applications. No R5 change is needed for a Linux-only feature that preserves the pulse protocol and timing.

## 11. Camera capture and photo storage

The grabber uses V4L2 with memory-mapped buffers, RGB24, 1280 x 720 requested resolution and 20 warmup frames. Single mode saves the final warmup frame; burst mode saves that frame and nine subsequent frames (29 total frames retrieved). Each image is written as binary PPM with a unique realtime timestamp filename. The actual negotiated resolution is logged.

```c
#include "grabber.h"
Grabber_capture();          // asynchronous single photo
Grabber_captureBurst(10);   // asynchronous burst
Grabber_cleanup();          // wait for capture to finish
```

`Grabber_isCapturing()` reports whether capture is active. Completion does not guarantee success; inspect the saved-photo count and logs. Do not call the grabber while FFmpeg owns the camera. Do not call cleanup from the capture thread itself.

Photos are stored on the target, not NFS:

```text
/var/lib/photobooth/photos/photo_<seconds>_<nanoseconds>.ppm
```

Each 1280 x 720 RGB image is approximately 2.64 MiB; ten photos require approximately 26.37 MiB. Monitor free space. The target clock was previously incorrect, so filenames may have incorrect wall-clock dates. Use modification time for local testing and configure time synchronization if accurate timestamps are required.

### 11.1 View latest photo (TARGET then HOST)

TARGET:

```bash
ls -lht /var/lib/photobooth/photos/ | head
cp "$(ls -t /var/lib/photobooth/photos/*.ppm | head -n 1)" /mnt/remote/test_latest.ppm
```

HOST:

```bash
xdg-open ~/ensc351/public/test_latest.ppm
```

### 11.2 Inspect latest burst (TARGET then HOST)

Run after the burst has finished. Use a fresh folder so older test photos are not confused with the current burst.

```bash
folder=$(mktemp -d /mnt/remote/burst_test.XXXXXX)
ls -t /var/lib/photobooth/photos/*.ppm | head -n 10 | while IFS= read -r photo; do
    cp "$photo" "$folder/"
done
printf 'Copied to %s\n' "$folder"
```

HOST: open the corresponding `burst_test.*` folder under `~/ensc351/public`. Confirm ten distinct images, proper exposure, and no corruption. The last-ten method assumes no intervening capture and a successful full burst.

### 11.3 Web integration notes

Browsers generally do not display PPM directly. The website team should convert completed files to JPEG or PNG, e.g. `ffmpeg -i input.ppm -frames:v 1 output.jpg`, and serve them through its own controlled storage/API. Avoid reading partially written files; coordinate with the capture completion event or implement atomic publication (temporary filename followed by rename). Do not expose the entire target filesystem. Define retention and deletion behaviour; photos may contain personal information.

## 12. Integration guidance for remaining group components

The current main loop owns countdown state, sensor reading, R5 pulses and camera lifecycle. Reuse that logic rather than starting separate camera or GPIO-control processes.

- **Buttons:** implement GPIO input/debouncing in the assigned HAL, then translate presses into the same capture/cancel requests used by keyboard controls. Confirm unused GPIO lines and pinmux before wiring. Do not use the R5 trigger or NeoPixel data lines as button inputs.
- **Audio:** coordinate with the existing countdown start/cancel events; avoid blocking the Linux loop for three seconds. Stop audio on cancellation.
- **Website:** define a thread-safe command interface to the Linux application, route single/burst/cancel through the same owner of countdown state, and return busy/error responses. Do not invoke a second photo booth process for each HTTP request. Keep capture work off the web event loop.
- **Concurrency:** the grabber mutex prevents overlapping grabber threads but does not protect the whole preview/countdown pipeline. If adding threads, serialize all capture requests in the main controller or use a properly synchronized queue. Existing cancellation is countdown-only.
- **Build integration:** add new source files to the relevant CMake target; do not duplicate HAL libraries. Preserve existing target names and deployment paths unless the whole team agrees to change them.

## 13. Testing checklist

| Test | Expected result |
|---|---|
| Fresh boot and startup | R5 running, live preview visible |
| Preview idle for several minutes | No repeated DTS or frame duplication warnings |
| Single capture in bright light | One photo, no flash, preview resumes |
| Single capture in low light | One illuminated photo |
| Burst in bright light | Ten distinct photos, preview resumes |
| Burst in low light | All ten photos illuminated |
| Cancel single countdown | LEDs stop, no new photo |
| Cancel burst countdown | LEDs stop, no new photos |
| Repeated Enter/B during countdown | Overlapping request rejected |
| Request during remaining flash | R5 busy guard rejects request |
| P during countdown or R5 busy window | Request rejected |
| Ctrl+C while idle | Preview stops, display clears |
| Restart application | Camera and GPIO available again |
| Check storage | Adequate space and correct photo count |

Record actual logs and test conditions. Previously observed single capture duration was 1537 ms with a two-second flash. One ten-photo burst produced ten files spanning approximately 604 ms between first and last filename timestamps (about 14.9 frames/s over nine intervals). These are measured examples, not worst-case guarantees. The 2500 ms flash passed initial testing; confirm all frames in several low-light bursts before a demo.

## 14. Troubleshooting

### west: unknown command build

Activate the virtual environment and run from `~/zephyrproject/zephyr`. Check `west topdir`. If missing, finish workspace initialization and module update.

### Zephyr board not found / SDK missing

Verify board target, pinned revision, course patches and SDK installation. Use a pristine build when changing board or toolchain. Check `df -h` if downloads/builds fail.

### NFS mount fails

Check both IP addresses, USB passthrough, `ping`, host `sudo exportfs -v`, NFS service and export permissions. Check `mountpoint /mnt/remote` before copying. Do not accidentally copy into an unmounted local directory.

### Camera device busy

```bash
sudo fuser -v /dev/video3
pgrep -a ffmpeg
v4l2-ctl --list-devices
```

Stop the old photo booth process normally. If an orphan FFmpeg process owns the camera, terminate that specific PID after identifying it. Do not kill unrelated processes. The preview must exit before capture starts. If the camera device number changed, update both main and grabber consistently or use a verified persistent `/dev/v4l/by-id` path.

### FFmpeg non-monotonic DTS / duplicated frames

The working preview filter includes `settb=1/30,setpts=N`, `-enc_time_base 1:30`, and `-fps_mode passthrough`. Preserve these arguments. They generate sequential preview timestamps; they do not change the saved photo resolution or force the physical camera to run at 30 FPS. Verify the target FFmpeg version supports them. Do not merely suppress warnings.

### Display blank / cursor visible

Check HDMI connection and `/dev/fb0`. The startup script forces HDMI on using `/sys/class/drm/card0-HDMI-A-1/status` and hides the tty1 cursor where supported. Connector paths can differ. Inspect `ls /sys/class/drm` rather than assuming the path. Framebuffer clearing at shutdown is intentional.

### SPI permission denied / sensor failure

Run through `sudo start_photobooth`, check `/dev/spidev0.0`, ADC wiring, common ground and reference voltage. Verify the sensor responds to covering/uncovering before changing the flash threshold.

### Flash ends before last burst frame

Check `[TIMING]` and inspect first/last photos. Capture latency depends on camera startup, exposure, frame rate and storage. If increasing flash duration, verify power/thermal limits and update both Linux and R5 constants. Rebuild both. Do not reduce the warmup frame count without checking exposure quality.

### Compiler reports expected declaration or statement at end of input

A previous copy of `grabber.c` was truncated near line 94, causing many cascading unused-variable errors. Check `wc -l photoGrabber/grabber.c` and `tail -n 20 photoGrabber/grabber.c`. Restore the complete committed source; do not disable `-Werror` to hide the problem.

### No photos / missing photos

Check the application log, directory existence, free space (`df -h /var/lib/photobooth/photos`), and saved-photo count. The script creates the photo directory. A capture operation finishing does not imply every image was successfully written.

## 15. Quick reference — normal development cycle

HOST, Linux-only changes:

```bash
cd ~/ensc351/work/ENSC351-Autonomous-Photobooth
cmake --build build-arm64 -j$(nproc)
```

HOST, R5 changes:

```bash
source ~/zephyr-venv/bin/activate
cd ~/zephyrproject/zephyr
west build -d ~/ensc351/work/ENSC351-Autonomous-Photobooth/build-r5
cp ~/ensc351/work/ENSC351-Autonomous-Photobooth/build-r5/zephyr/zephyr.elf ~/ensc351/public/photobooth-r5-cancel.elf
```

TARGET (after stopping previous instance):

```bash
sudo start_photobooth
```

HOST, commit reviewed source changes:

```bash
git status
git diff
# Stage only the intended source/documentation files.
git add -u
git add README.md
git diff --cached
git commit -m "Document photo booth setup and integration"
git push
```

Stage new source files explicitly if needed. Never stage generated builds or personal photos accidentally.

## 16. Handover limitations and references

This guide documents the working development procedure and current source interfaces. The exact Git URL, original Linux CMake configure command/sysroot, Zephyr commit, SDK version, final wiring diagram and full startup/R5 source were not available for verification when writing this guide. Fill these in from the working checkout before relying on a completely fresh installation. Fresh-install steps are guidance, not a tested reproduction on a second machine.

Preserve the copyright and GPLv2 notices in the V4L2 grabber source (Mauro Carvalho Chehab; modifications by Derek Molloy). Consult the course guides for BeagleY-AI networking, cross-compilation and R5 setup, and the version-matched Zephyr documentation for SDK installation. Additional reference: https://docs.zephyrproject.org/ .