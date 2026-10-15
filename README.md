<p align="center">
  <img src="assets/kiln-icon.png" width="160" alt="Kiln">
</p>

<h1 align="center">Kiln</h1>

<p align="center"><b>Android, fired up natively on your Mac.</b><br>
An Android emulator built for Apple Silicon: hardware virtualization, real GPU acceleration, and controls made for games.</p>

<p align="center"><code>0.0.3-alpha</code> · macOS 14+ · Apple Silicon (M1 and later) · English and Português (Brasil)</p>

<p align="center"><a href="https://github.com/NKS1608/kiln-android-emulator/releases/tag/v0.0.3-alpha"><b>⬇ Download Kiln 0.0.3-alpha</b></a> · <a href="https://ko-fi.com/nks1608">☕ Support Kiln</a></p>

---

> **This is an alpha.** It runs Android 16 well enough for daily testing and real games: expect rough edges, and please
> tell us what breaks (see [Reporting a problem](#reporting-a-problem)).
> Enjoying it? [Support Kiln on Ko-fi](https://ko-fi.com/nks1608).

<p align="center">
  <img src="assets/android-home.png" width="300" alt="Android 16 running in Kiln, with Google Play">
</p>

## Why Kiln

- **Native speed on Apple Silicon.** Kiln runs a real arm64 Android on Apple's Hypervisor framework, with no instruction
  translation in the way. The CPU runs at native speed, and a new instance reaches the home screen in about half a minute.
- **Real GPU acceleration.** OpenGL ES 3.1 and Vulkan in the guest are rendered on your Mac's GPU through Metal. Games pick
  their Vulkan renderer just as they would on a flagship phone.
- **Android 16, our own build.** The bundled image is Android 16 (LineageOS 23.2) built from source by Kiln, signed with
  Kiln's own keys and running with SELinux enforcing, with Google Play included for app compatibility testing (see
  [About the Android image](#about-the-android-image)). Create an instance and you land straight on the home screen: no
  setup wizard, no lock screen.
- **Made for games.**
  - **Key mapping per game.** Drag taps, a WASD joystick and swipes onto the screen from a side panel, then press the key.
    Combos such as ⇧Q or ⌃1 work, and several keys can be held at once. Each game keeps its own mapping, and Kiln switches
    to it as soon as the game comes to the front. Press F10 to type in a game's chat.
  - **Game controllers.** A controller connected to your Mac shows up in Android as a standard gamepad.
  - **Mouse look** for shooters (in the default mapping), rotation that follows the game, and refresh rates up to 144 Hz.
- **Every instance is its own phone.** Pick from 16 device profiles (Samsung Galaxy S24 Ultra, Pixel 9 Pro, OnePlus 12,
  ROG Phone 8 and more): Android reports that phone's model, build fingerprint, baseband, board, system-on-chip and SKU. Each instance has its own
  SIM card (39 operators in 15 countries, or none) with a phone number of its country, IMEI, serial number and home
  Wi-Fi network. The battery drains and
  charges like a real one, and the mobile signal comes and goes.
- **Many instances.** Run several side by side, each in its own window and with its own Dock icon color. Choose the GPU a
  game should see (Adreno 740 or 650, Mali-G68) and size CPU and memory with Low, Medium or High presets, which scale with your Mac's performance cores and memory.
- **Install anything.** Install `.apk`, `.xapk`, `.apkm` and `.apks` bundles from the toolbar: split APKs are put
  together for you.
- **A developer's emulator too.** Open a shell inside Android with one click, with a real terminal (job control, `top`,
  `vi`) and no adb needed. Every instance logs its kernel, logcat and emulator output, and **Error Report** zips them
  for you. adb is there when you want it, opt-in per instance and reachable from your Mac only.

## Install

1. Download **`Kiln-0.0.3-alpha.dmg`** from [Releases](https://github.com/NKS1608/kiln-android-emulator/releases/tag/v0.0.3-alpha) and open it.
2. Drag **Kiln** to **Applications**.
3. The first time only: this alpha is not notarized yet, so **right-click Kiln → Open**, then confirm. You can also use
   *System Settings → Privacy & Security → Open Anyway*.
4. Click **+** to create an instance, then **Start**.

Each instance gets its own disks, about 3 GB on your Mac to start, growing as you install apps (the storage size you
choose is what Android shows; your Mac only stores what is written). Plan on 4 GB of RAM per running
instance with the Low preset.

**Coming from an earlier alpha?** Your instances keep working with the image they were created with. The new Android image
is used by instances you create after updating: create a new one to get it (apps and data do not move between images).
The new phone details (system-on-chip, board, SKU) and the faster start appear only on instances made from the new image.

## About the Android image

- **Android:** LineageOS 23.2 (Android 16), built from the LineageOS sources by Kiln for the `virtio_arm64only` device,
  with Kiln's graphics drivers, input, guest agent and fake modem built in. It is a `user` build signed with Kiln's own
  keys (`release-keys`), with SELinux enforcing. Thanks to jqssun's
  [android-lineage-qemu](https://github.com/jqssun/android-lineage-qemu), whose images earlier versions of Kiln shipped.
- **Google apps:** only Google Play Store, Google Play services and the Google Services Framework, from
  [MindTheGapps](https://github.com/MindTheGapps) (Android 16), included **for compatibility testing only**, so that apps
  which depend on Google Play services can be tried in this alpha. They are not part of LineageOS and may be removed or
  replaced in later releases.
- **Left out on purpose:** music player, recorder, calculator, clock, calendar, contacts, messaging, the setup wizard,
  always-on display, hotspot and tethering, Bluetooth and casting.

## Using Kiln

| To | Do |
| --- | --- |
| Create an instance | **+** in the launcher (pick the phone, screen and performance in its settings) |
| Back / Home / Recents | Toolbar buttons, or right click / middle click |
| Volume | Toolbar **−** and **+**, like a phone's volume keys |
| Install an app | Toolbar **Install APK**, then choose `.apk` / `.xapk` / `.apkm` / `.apks` files |
| Choose the storage size | Instance settings → **Storage**, before its first start |
| Choose the SIM's country | Instance settings → **SIM** (or **None**); **New** draws another SIM |
| Map keys for a game | Open the game, then toolbar **Key Mapping** (⌘K) |
| Turn a game's mapping off and on | **F10** |
| Mouse look (default mapping) | **`** (backtick) |
| Switch portrait / landscape | Toolbar **Rotate** (⌘R) |
| Open a shell in Android | **Shell** in the launcher, while the instance runs |
| Save the logs for a bug report | **Error Report** in the launcher |

Power users get the same through `kilnctl`, the command-line tool inside the app:
`/Applications/Kiln.app/Contents/Helpers/kilnctl` (run it alone for the list of commands).

## What's new in 0.0.3-alpha

- **Starts on macOS 14 and later.** The 0.0.2-alpha download was built for the newest macOS only and did not start on older
  ones. The runtime is now built for macOS 14 (and is about 90 MB smaller).
- **A faster start.** The new Android image no longer goes through a UEFI firmware step: Kiln hands it the kernel and the
  phone's identity directly, which saves about 0.6 seconds on every boot.
- **A more convincing phone.** Device information apps now show the phone's chip and board (for example a Snapdragon 8 Gen 2
  for a Galaxy S23 Ultra, a Tensor G3 for a Pixel 8) and an SKU in the maker's usual shape, instead of "VirtIO
  virtio_arm64only". The hardware name still reads `virtio`, because Android finds its drivers by that name.
- **Presets that fit your Mac.** Low, Medium and High now follow your Mac's performance cores and memory: on a Mac with
  four performance cores and 16 GB, High is 4 CPUs and 6 GiB instead of 6 CPUs and 8 GiB, which crowded the efficiency
  cores.
- **Less work for a hidden game.** While an instance's window is minimized or covered, it renders at 10 frames per second
  and returns to full speed when you look at it again.
- **Steadier.** A malformed command from Android no longer crashes the player, Stop no longer cuts off Android while it is
  still saving, and several memory-safety problems in the graphics path are fixed (memory the renderer freed could still be
  reached from inside Android, and two race conditions between Android and the renderer).

## Known limitations

- **Alpha quality.** Some things will break. Back up anything you care about inside an instance.
- **macOS 14 to 26 are untested for full sessions.** The app and its runtime start on macOS 14; a complete Android session
  has been run on macOS 27 only. Reports from older systems are very welcome.
- **Not notarized.** macOS asks for confirmation the first time (see [Install](#install)).
- **Google certification.** This is an emulator, so apps that require Play Integrity / device certification refuse to
  run, and games or apps that look for emulators can still tell. Signing in to a Google account has not been fully
  tested yet.
- **One Android version.** Android 16 only for now. More versions are planned.
- **No microphone, camera, Bluetooth, calls or mobile data** inside Android yet: the SIM registers on its network, and the
  internet comes from the Wi-Fi. Without Bluetooth, the Play Store hides the few apps that require it.
- **Storage is set once.** Choose it before the instance's first start.
- **`adb root` is not available** in the new image (it is a `user` build). `adb shell` works, without any key to set up.
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
android-lineage-qemu project, MindTheGapps, any mobile operator or any device maker named here.

## Files in the release

| File | What |
| --- | --- |
| `Kiln-0.0.3-alpha.dmg` | The app, with the Android 16 image inside |
| `SHA256SUMS.txt` | Checksum of the disk image |
| `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt`, `OFFER.txt` | Licensing |
