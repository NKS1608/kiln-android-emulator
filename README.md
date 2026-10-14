<p align="center">
  <img src="assets/kiln-icon.png" width="160" alt="Kiln">
</p>

<h1 align="center">Kiln</h1>

<p align="center"><b>Android, fired up natively on your Mac.</b><br>
An Android emulator built for Apple Silicon: hardware virtualization, real GPU acceleration, and controls made for games.</p>

<p align="center"><code>0.0.1-alpha</code> · macOS 14+ · Apple Silicon (M1 and later)</p>

<p align="center"><a href="https://github.com/NKS1608/kiln-android-emulator/releases/latest"><b>⬇ Download Kiln 0.0.1-alpha</b></a> · <a href="https://ko-fi.com/nks1608">☕ Support Kiln</a></p>

---

> **This is an alpha.** It runs Android 16 well enough for daily testing and real games, and it is the first public
> build: expect rough edges, and please tell us what breaks (see [Reporting a problem](#reporting-a-problem)).
> Enjoying it? [Support Kiln on Ko-fi](https://ko-fi.com/nks1608).

<p align="center">
  <img src="assets/android-home.png" width="300" alt="Android 16 running in Kiln, with Google Play">
</p>

## Why Kiln

- **Native speed on Apple Silicon.** Kiln runs a real arm64 Android on Apple's Hypervisor framework, with no instruction
  translation in the way. The CPU runs at native speed, and a new instance reaches the home screen in about half a minute.
- **Real GPU acceleration.** OpenGL ES 3.1 and Vulkan in the guest are rendered on your Mac's GPU through Metal. Games pick
  their Vulkan renderer just as they would on a flagship phone.
- **Android 16, ready to go.** The bundled image is Android 16 (LineageOS 23.2) with Google Play included for app
  compatibility testing (see [About the Android image](#about-the-android-image)). Create an instance and you land straight
  on the home screen: no setup wizard, no lock screen.
- **Made for games.**
  - **Key mapping per game.** Drag taps, a WASD joystick and swipes onto the screen from a side panel, then press the key.
    Combos such as ⇧Q or ⌃1 work, and several keys can be held at once. Each game keeps its own mapping, and Kiln switches
    to it as soon as the game comes to the front. Press F10 to type in a game's chat.
  - **Game controllers.** A controller connected to your Mac shows up in Android as a standard gamepad.
  - **Mouse look** for shooters (in the default mapping), rotation that follows the game, and refresh rates up to 144 Hz.
- **Many phones, many instances.** Run several instances side by side, each in its own window and with its own Dock icon
  color. Pick from 16 device profiles (Samsung Galaxy S24 Ultra, Pixel 9 Pro, OnePlus 12, ROG Phone 8 and more), or the
  GPU a game should see (Adreno 740 or 650, Mali-G68). Size CPU and memory with Low, Medium or High presets.
- **Install anything.** Install `.apk`, `.xapk`, `.apkm` and `.apks` bundles from the toolbar: split APKs are put
  together for you.
- **A developer's emulator too.** Open a shell inside Android with one click, with a real terminal (job control, `top`,
  `vi`) and no adb needed. Every instance logs its kernel, logcat and emulator output, and **Error Report** zips them
  for you. adb is there when you want it, opt-in per instance and reachable from your Mac only.

## Install

1. Download **`Kiln-0.0.1-alpha.dmg`** (1.5 GB) from [Releases](https://github.com/NKS1608/kiln-android-emulator/releases/latest) and open it.
2. Drag **Kiln** to **Applications**.
3. The first time only: this alpha is not notarized yet, so **right-click Kiln → Open**, then confirm. You can also use
   *System Settings → Privacy & Security → Open Anyway*.
4. Click **+** to create an instance, then **Start**.

Each instance gets its own disks, about 3 GB to start, growing as you install apps. Plan on 4 GB of RAM per running
instance with the Low preset.

## About the Android image

- **Android:** LineageOS 23.2 (Android 16) for QEMU, device `virtio_arm64only`, as built and published by the
  [android-lineage-qemu](https://github.com/jqssun/android-lineage-qemu) project by jqssun, installed from its OTA package.
  Kiln adds its own files to the `/vendor` partition only: graphics drivers, input, the guest agent and boot settings.
- **Google apps:** MindTheGapps 16.0.0 (arm64, build 20260915), included **for compatibility testing only**, so that apps
  which depend on Google Play services can be tried in this alpha. It is not part of LineageOS and may be removed or
  replaced in later releases.

## Using Kiln

| To | Do |
| --- | --- |
| Create an instance | **+** in the launcher (pick the phone, screen and performance in its settings) |
| Back / Home / Recents | Toolbar buttons, or right click / middle click |
| Install an app | Toolbar **Install APK**, then choose `.apk` / `.xapk` / `.apkm` / `.apks` files |
| Map keys for a game | Open the game, then toolbar **Key Mapping** (⌘K) |
| Turn a game's mapping off and on | **F10** |
| Mouse look (default mapping) | **`** (backtick) |
| Rotate | Toolbar **Rotate** (⌘R) |
| Open a shell in Android | **Shell** in the launcher, while the instance runs |
| Save the logs for a bug report | **Error Report** in the launcher |

Power users get the same through `kilnctl`, the command-line tool inside the app:
`/Applications/Kiln.app/Contents/Helpers/kilnctl` (run it alone for the list of commands).

## What's in 0.0.1-alpha

- Android 16 (LineageOS 23.2) image, with Google Play for compatibility testing; first boot straight to the home screen.
- GPU acceleration (GLES 3.1 and Vulkan), sound, rotation, game controllers.
- Per-game key mapping editor: taps, joystick, swipes, modifier combos, several keys at once.
- Device profiles, GPU profiles and performance presets per instance.
- APK and split-bundle install, a built-in shell with a real terminal, logcat on the host, and one-click error reports.

## Known limitations

- **Alpha quality.** Some things will break. Back up anything you care about inside an instance.
- **Not notarized.** macOS asks for confirmation the first time (see [Install](#install)).
- **Google certification.** This is an emulator, so apps that require Play Integrity / device certification may refuse to
  run. Signing in to a Google account has not been fully tested yet.
- **One Android version.** Android 16 only for now. More versions are planned.
- **No microphone, camera or Bluetooth** inside Android yet.
- **"Limited connection" notice.** On the very first boot Android may briefly show it while the network settles. It goes
  away by itself.
- Mouse look works with the default mapping (F10 in a game that has no mapping of its own). The editor cannot place it
  yet, so a game with its own mapping has no mouse look.

## Support Kiln

Kiln is free. If it is useful to you, you can help it grow:

<p align="center"><a href="https://ko-fi.com/nks1608"><b>☕ Support Kiln on Ko-fi</b></a></p>

The first goal is an Apple Developer account, so that Kiln can be notarized and opens without the security warning.
After that: more Android versions, and more Macs to test on. Donations are
voluntary and do not unlock anything: every feature stays available to everyone.

You will also find **Support Kiln…** in the Kiln menu of the app.

## Reporting a problem

Click **Error Report** in the launcher. It saves a zip with the instance's settings and logs in your Downloads folder.
Attach it to your report, along with what you did and what you expected. Nothing is sent anywhere by the app.

Contact: **[contato@nks1608.tec.br](mailto:contato@nks1608.tec.br)**

## Licensing

Kiln (the launcher, the player and `kilnctl`) is proprietary software. It ships open-source components under their own
licenses:
- QEMU and Kiln's virtual devices, under GPL-2.0-or-later. Their complete source code is available on request under
  the written offer in `OFFER.txt`, using the contact address above.
- Permissive components, listed in `THIRD-PARTY-NOTICES.txt`.

The full licensing map is in `LICENSE.txt`.

Android is a trademark of Google LLC. Google Play is a trademark of Google LLC. The Android robot is reproduced or
modified from work created and shared by Google and used according to terms described in the Creative Commons 3.0
Attribution License. Kiln is not affiliated with or endorsed by Google, the LineageOS project, the
android-lineage-qemu project or any device maker named here.

## Files in the release

| File | What |
| --- | --- |
| `Kiln-0.0.1-alpha.dmg` | The app, with the Android 16 image inside |
| `SHA256SUMS.txt` | Checksum of the disk image |
| `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt`, `OFFER.txt` | Licensing |
