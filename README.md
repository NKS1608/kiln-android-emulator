<p align="center">
  <img src="assets/kiln-icon.png" width="160" alt="Kiln">
</p>

<h1 align="center">Kiln</h1>

<p align="center"><b>Android, fired up natively on your Mac.</b><br>
An Android emulator built for Apple Silicon: hardware virtualization, real GPU acceleration, and controls made for games.</p>

<p align="center"><code>0.0.7-beta</code> · macOS 14+ · Apple Silicon (M1 and later) · English and Português (Brasil)</p>

<p align="center"><a href="https://github.com/NKS1608/kiln-android-emulator/releases/tag/v0.0.7-beta"><b>⬇ Download Kiln 0.0.7-beta</b></a> · <a href="https://ko-fi.com/nks1608">☕ Support Kiln</a></p>

---

> **This is a beta.** It runs Android 16 well enough for daily use and real games: some rough edges remain, so please
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
    to it as soon as the game comes to the front. Press ⌃⌥ (Control+Option) to type in a game's chat.
  - **Camera control with the mouse.** A mouse area turns mouse movement into a thumb on the game's camera; the
    mouse buttons can be keys of their own, and the joystick takes any four keys.
  - **Game controllers.** A controller connected to your Mac shows up in Android as a standard gamepad, and only while it
    is connected.
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

1. Download **`Kiln-0.0.7-beta.dmg`** from [Releases](https://github.com/NKS1608/kiln-android-emulator/releases/tag/v0.0.7-beta) and open it.
2. Drag **Kiln** to **Applications**.
3. The first time only: this beta is not notarized yet, so **right-click Kiln → Open**, then confirm. You can also use
   *System Settings → Privacy & Security → Open Anyway*.
4. Click **+** to create an instance, then **Start**.

Each instance gets its own disks, about 3 GB on your Mac to start, growing as you install apps (the storage size you
choose is what Android shows; your Mac only stores what is written). Plan on 4 GB of RAM per running
instance with the Low preset.

**Coming from an alpha?** Instances made with an earlier version show **Update** in the launcher: select one while it
is stopped and click **Update Android**. It gets this version's Android and keeps its apps, data and accounts. The old
system is kept: delete it with **Delete Previous System** once the instance works, or use **Go Back to Previous
System** if something is wrong. Most of what is new
below lives inside Android (graphics, sound, touch), so an instance gets it once updated. Instances from the first two
alphas, which used another Android image, cannot be updated in place: create a new instance for them.

## About the Android image

- **Android:** LineageOS 23.2 (Android 16), built from the LineageOS sources by Kiln for the `virtio_arm64only` device,
  with Kiln's graphics drivers, input, guest agent and fake modem built in. It is a `user` build signed with Kiln's own
  keys (`release-keys`), with SELinux enforcing. Thanks to jqssun's
  [android-lineage-qemu](https://github.com/jqssun/android-lineage-qemu), whose images earlier versions of Kiln shipped.
- **Google apps:** only Google Play Store, Google Play services and the Google Services Framework, from
  [MindTheGapps](https://github.com/MindTheGapps) (Android 16), included **for compatibility testing only**, so that apps
  which depend on Google Play services can be tried in this beta. They are not part of LineageOS and may be removed or
  replaced in later releases.
- **Left out on purpose:** music player, recorder, calculator, clock, calendar, contacts, messaging, the setup wizard,
  always-on display, hotspot and tethering, and casting.

## Using Kiln

| To | Do |
| --- | --- |
| Create an instance | **+** in the launcher (pick the phone, screen and performance in its settings) |
| Get a newer Android for an instance | **Update Android** in the launcher, with the instance stopped (apps and data stay) |
| Back / Home / Recents | Toolbar buttons; middle click is Home |
| Long press (menus, text selection, moving icons) | Right click |
| Volume | Toolbar **−** and **+**, like a phone's volume keys |
| Install an app | Toolbar **Install APK**, then choose `.apk` / `.xapk` / `.apkm` / `.apks` files |
| Choose the storage size | Instance settings → **Storage**, before its first start |
| Choose the SIM's country | Instance settings → **SIM** (or **None**); **New** draws another SIM |
| Map keys for a game | Open the game, then toolbar **Key Mapping** (⌘K) |
| Turn a game's mapping off and on, and free or hold the mouse | **⌃⌥** (Control+Option), or F10 |
| Move a game's camera with the mouse | Key Mapping → **Mouse Area**, placed over the camera |
| Mouse look (default mapping) | **`** (backtick) |
| Full screen with only the game | The window's green button, or ⌃⌘F |
| Switch portrait / landscape | Toolbar **Rotate** (⌘R) |
| Open a shell in Android | **Shell** in the launcher, while the instance runs |
| Save the logs for a bug report | **Error Report** in the launcher |

Power users get the same through `kilnctl`, the command-line tool inside the app:
`/Applications/Kiln.app/Contents/Helpers/kilnctl` (run it alone for the list of commands).

## What's new in 0.0.7-beta

- **OpenGL ES games run like on a phone.** Android now gets real GPU fences from the Mac, as a phone's graphics driver
  gives them: no app or game waits for the whole GPU any more, and none can run far ahead of it either. Genshin Impact on
  an OpenGL ES profile played 40 minutes without closing in our tests, where it used to close within 2 to 12, and the
  browser keeps its sharpness with no flicker.
- **Sound about 100 ms closer to the picture.** Android's sound now leaves in 21 ms steps instead of 85, and Kiln starts
  playing sooner.
- **Faster, more natural input.** The mouse and keyboard go straight to Android without passing through an emulated USB
  mouse and keyboard, so keys arrive up to 8 ms sooner and the pointer follows at once. Touches land with a finger's
  small offset, tremble and varying pressure instead of on the exact same pixel every time.
- **Key mapping, more of it.** Place a **mouse area** over a game's camera: moving the mouse drags a thumb there, which
  lifts and lands again at the centre when it runs out of room or the mouse rests. Mouse buttons can be keys of their
  own, the joystick takes any four keys, and **⌃⌥** turns the mapping off and on and frees or holds the mouse.
- **Right click is a long press,** the touch screen's way to open menus, select text and move icons. The wheel scrolls
  the way the window shows the screen, in landscape too.
- **Full screen is the game alone,** with no toolbar. A controller appears in Android only while one is connected to
  your Mac, and the "show taps" dots are gone.
- **Update Android without losing anything.** Instances made with an earlier version get this version's Android with
  one click in the launcher, keeping their apps, data and accounts.
- **Android starts in about 20 seconds.**

## Known limitations

- **Beta quality.** Some things may still break. Back up anything you care about inside an instance.
- **macOS 14 to 26 are untested for full sessions.** The app and its runtime start on macOS 14; a complete Android session
  has been run on macOS 27 only. Reports from older systems are very welcome.
- **Not notarized.** macOS asks for confirmation the first time (see [Install](#install)).
- **Google certification.** This is an emulator, so apps that require Play Integrity / device certification refuse to
  run, and games or apps that look for emulators can still tell. Signing in to a Google account has not been fully
  tested yet.
- **One Android version.** Android 16 only for now. More versions are planned.
- **No microphone, camera, calls or mobile data** inside Android yet: the SIM registers on its network, and the internet
  comes from the Wi-Fi. Bluetooth is virtual and cannot pair with real devices.
- **Some games may still show rendering glitches.** Please report the game, its graphics profile (Adreno 740, 650 or
  Mali-G68) and a screenshot.
- **M1 and M2 Macs are still untested.** 0.0.4 sized the graphics memory window to fit them, but nobody has reported
  back yet: if you have an M1 or M2 Mac, please tell us whether Kiln starts.
- **OpenGL ES games can still dip below 60 fps** in heavy scenes, and Genshin Impact's in-game menu is slow on an
  OpenGL ES profile. Vulkan profiles (such as Adreno 740) are usually smoother.
- **Genshin Impact on an OpenGL ES profile can still close** after a long session. This beta includes a fix for one
  possible cause on Kiln's side; please report how long it plays for you. Vulkan profiles (such as Adreno 740) are not
  affected.
- **Storage is set once.** Choose it before the instance's first start.
- **`adb root` is not available** in the new image (it is a `user` build). `adb shell` works, without any key to set up.
- **"Limited connection" notice.** On the very first boot Android may briefly show it while the network settles. It goes
  away by itself.
- **No Widevine.** Video apps that need Google's Widevine DRM do not play protected content.

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

Report it as an [issue on GitHub](https://github.com/NKS1608/kiln-android-emulator/issues). A Kiln Discord server is
coming soon, for reports, questions and news.

## Licensing

Kiln (the launcher, the player and `kilnctl`) is proprietary software. It ships open-source components under their own
licenses:
- QEMU and Kiln's virtual devices, under GPL-2.0-or-later. Their complete source code is available on request under
  the written offer in `OFFER.txt`, through the [GitHub issues](https://github.com/NKS1608/kiln-android-emulator/issues).
- Permissive components, listed in `THIRD-PARTY-NOTICES.txt`.

The full licensing map is in `LICENSE.txt`.

Android is a trademark of Google LLC. Google Play is a trademark of Google LLC. The Android robot is reproduced or
modified from work created and shared by Google and used according to terms described in the Creative Commons 3.0
Attribution License. Kiln is not affiliated with or endorsed by Google, the LineageOS project, the
android-lineage-qemu project, MindTheGapps, any mobile operator or any device maker named here.

## Files in the release

| File | What |
| --- | --- |
| `Kiln-0.0.7-beta.dmg` | The app, with the Android 16 image inside |
| `SHA256SUMS.txt` | Checksum of the disk image |
| `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt`, `OFFER.txt` | Licensing |
