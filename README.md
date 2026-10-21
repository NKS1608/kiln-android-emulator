<p align="center">
  <img src="assets/kiln-icon.png" width="160" alt="Kiln">
</p>

<h1 align="center">Kiln</h1>

<p align="center">An Android emulator for Macs with Apple Silicon, made for apps and games.</p>

<p align="center"><code>0.1.2</code> · macOS 14 or later · Apple Silicon (M1 and later) · English and Português (Brasil)</p>

<p align="center"><a href="https://github.com/NKS1608/kiln-android-emulator/releases/tag/v0.1.2"><b>Download Kiln 0.1.2</b></a> · <a href="https://ko-fi.com/nks1608">Support Kiln</a></p>

---

<p align="center">
  <img src="assets/android-home.png" width="300" alt="Android 16 running in Kiln">
</p>

Kiln runs Android 16 in a window on your Mac. Each instance behaves like a separate phone, with its own apps, settings
and device identity, and you can run several at once. Graphics run on your Mac's GPU, and games can be played with the
keyboard, the mouse or a game controller.

## Features

### Android

- Android 16, built by the Kiln project and included in the app. A new instance starts in about half a minute and
  opens directly on the home screen.
- Apps and games get OpenGL ES 3.1 and Vulkan, drawn by your Mac's GPU.
- Install `.apk`, `.xapk`, `.apkm` and `.apks` files from the toolbar. Split APKs are combined for you.
- Google apps are not included, but can be added with **Install Zip** (see [About the Android image](#about-the-android-image)).

### Games

- Key mapping per game. Place taps, a joystick and swipes on the screen, then assign keys to them. Key combinations
  such as ⇧Q or ⌃1 work, and several keys can be held at once. Kiln switches to a game's mapping when that game comes
  to the front.
- Camera control with the mouse, for games where you look around by dragging a thumb on the screen.
- Gyroscope aiming: in games that aim by tilting the phone, moving the mouse turns it. You can choose a key or mouse
  button to hold while aiming, and keep the mouse camera for the rest of the time.
- A key of your choice frees the mouse pointer while a mapping is on, to click the game's menus. Hold ⌥ (or another
  modifier) to free it only while held.
- Share a game's mapping as a file with **Export…** and **Import…** in the mapping editor.
- A mouse and keyboard mode for games that support them directly, such as Minecraft.
- Game controllers connected to your Mac appear in Android as a standard gamepad.
- More mapping options are on the way, including repeated taps, MOBA skills, zoom and scroll, and controllers mapped
  to touches.
- Anti-aliasing in games, as on a phone: games such as PUBG Mobile at HD quality draw it correctly.
- Refresh rates up to 180 Hz. The screen's rate follows what is shown, as on phones with adaptive displays: a still
  screen slows down and a game gets the rate it runs at.

### Instances

- Choose from 16 phone models, including the Samsung Galaxy S24 Ultra, Pixel 9 Pro, OnePlus 12 and ROG Phone 8.
  Android reports the chosen phone's model and system details to apps.
- Each instance has its own SIM card (39 operators in 15 countries, or none), phone number, IMEI, serial number and
  Wi-Fi network. Its battery charges and drains like a real one.
- GPS at a place you choose, or along a route you import in the player (a GPX file or a list of coordinates), on
  foot, by bicycle, by car or at the times recorded in the file.
- Choose which GPU games see (Adreno 740, Adreno 650 or Mali-G68), and set CPU and memory with the Low, Medium or High
  preset.
- Each instance has its own window and Dock icon color. Closing the window can leave Android running: the instance
  stays in the Dock, and clicking it brings the window back.

### For developers

- A shell inside Android, one click from the launcher, with a full terminal.
- adb, turned on per instance and reachable only from your Mac.
- **Error Report** collects an instance's logs, a screenshot and a short screen recording in one zip file.
- `kilnctl`, a command-line tool for everything the launcher does.

## Install

1. Download `Kiln-0.1.2.dmg` from the [release page](https://github.com/NKS1608/kiln-android-emulator/releases/tag/v0.1.2)
   and open it.
2. Drag **Kiln** to **Applications**.
3. Kiln is not notarized by Apple yet, so macOS asks for confirmation the first time. Right-click Kiln, choose
   **Open**, and confirm. You can also allow it in System Settings > Privacy & Security.
4. In the launcher, click **+** to create an instance, then **Start**.

A new instance uses about 3 GB on your Mac and grows as you install apps. The storage size you choose is what Android
reports; your Mac only stores what is actually written. With the Low preset, plan on 4 GB of memory for each running
instance.

### Updating from an earlier version

Quit your instances, then replace Kiln in Applications. Instances made with an earlier version show **Update** in the
launcher. Select one while it is stopped and click **Update Android**: it gets the new Android and keeps its apps, data
and accounts. The previous system is kept until you remove it with **Delete Previous System**, and **Go Back to
Previous System** restores it if something goes wrong.

The first time a game opens after an update, it may take longer to load once.

## About the Android image

The included Android is LineageOS 23.2 (Android 16), built from source by the Kiln project and signed with Kiln's own
keys. Thanks to jqssun's [android-lineage-qemu](https://github.com/jqssun/android-lineage-qemu), whose images earlier
versions of Kiln used.

Google apps may not be redistributed, so Kiln does not include them. To use apps that need Google Play services,
download a GApps package for Android 16 (arm64), such as [MindTheGapps](https://github.com/MindTheGapps) or
[NikGApps](https://nikgapps.com) Core, and install it with **Install Zip**, ideally before the instance's first start.
Kiln is tested with both.

Some apps are left out of the image on purpose: music player, recorder, calculator, clock, calendar, contacts,
messaging and the setup wizard. Always-on display, hotspot, tethering and casting are also not included.

## Using Kiln

| Task | How |
| --- | --- |
| Create an instance | **+** in the launcher, then choose the phone, screen and performance in its settings |
| Update an instance's Android | **Update Android** in the launcher, with the instance stopped |
| Back, Home, Recents | Toolbar buttons. Middle click is Home |
| Long press | Right click |
| Volume | **−** and **+** in the toolbar |
| Install an app | **Install APK** in the toolbar |
| Install a flashable zip, such as GApps | **Install Zip** in the launcher, with the instance stopped |
| Open the recovery | **Recovery** in the launcher, with the instance stopped |
| Run a GSI instead of Kiln's Android | The arrow next to **+**, then **New Instance with a GSI…** |
| Set the storage size | Instance settings > **Storage**, before the first start |
| Set the SIM's country | Instance settings > **SIM**. **New** generates another SIM |
| Map keys for a game | Open the game, then **Key Mapping** in the toolbar (⌘K) |
| Turn a game's mapping on or off | ⌃⌥ (Control+Option) or F10. A game opens with its mapping off |
| Use mouse and keyboard as on a PC | Key Mapping > **Mouse and keyboard mode**, then ⌃⌥ |
| Control a game's camera with the mouse | Key Mapping > **Mouse Area**, placed over the camera |
| Aim by tilting, in games that support it | Key Mapping > **Gyroscope**, then ⌃⌥ |
| Free the mouse pointer while mapping | Set a key under **Free pointer** in Key Mapping |
| Share a mapping | Key Mapping > **Export…** or **Import…** |
| Follow a GPS route | **Location** > **Import Coordinates…** in the player, with GPS location on |
| Keep an instance in another folder | **Move…** in the launcher |
| Mouse look (default mapping) | ` (backtick) |
| Full screen | The window's green button, or ⌃⌘F |
| Rotate the screen | **Rotate** in the toolbar (⌘R) |
| Open a shell in Android | **Shell** in the launcher, while the instance runs |
| Collect logs for a bug report | **Error Report** in the launcher |

`kilnctl` is at `/Applications/Kiln.app/Contents/Helpers/kilnctl`. Run it without arguments to see its commands.

## What's new in 0.1.2

- **A new look.** The launcher shows each instance with its phone, status and a Start button at the top; settings that
  wait for the next start say so while Android runs. Phone models are chosen from a grid, and the whole app uses Kiln's
  green.
- **A toolbar that fits.** In a phone held upright, the window keeps Back, Home and Recents, Rotate, Key Mapping and Install
  APK, with the instance's name between them, and the rest under **More**. A window opens large enough to use, also when
  it was left small.
- **Screenshot** in the toolbar saves the screen to the Desktop.
- **Anti-aliasing in games.** Games that use it, such as PUBG Mobile at HD quality, draw their foliage and edges
  correctly.
- A new Android build. Instances show **Update** in the launcher, and their apps and data are kept.

More mapping options are coming in the next releases: repeated taps, MOBA skills, zoom and scroll, and game
controllers mapped to touches for games without controller support.

## Known limitations

- Keep backups of anything important inside an instance.
- Kiln has only been tested in full on macOS 27. It starts on macOS 14 and later, but complete sessions on macOS 14 to
  26 have not been tested. Reports are welcome.
- M1 and M2 Macs have not been tested. If you have one, please tell us whether Kiln works.
- Kiln is not notarized, so macOS asks for confirmation the first time it opens.
- Apps that require Play Integrity or device certification do not run, and some apps can detect that they are in an
  emulator. Signing in to a Google account has not been fully tested.
- Android 16 is the only version available. More versions are planned.
- Calls and SMS are not supported. Bluetooth is virtual and cannot pair with real devices.
- Hardware-backed key storage is available only in instances created with 0.0.9.2 or later.
- Some games may show graphics glitches. Please report the game, its graphics profile and a screenshot.
- OpenGL ES games can drop below 60 fps in their heaviest scenes, and Genshin Impact's menu can be slow on an OpenGL ES
  profile. Vulkan profiles, such as Adreno 740, are usually smoother.
- Genshin Impact on an OpenGL ES profile may close after a long session. Vulkan profiles are not affected.
- A GSI shows its own device name instead of the chosen phone, and its adb cannot be used. Use **Shell** in the launcher
  instead.
- The storage size is set once, before the instance's first start.
- `adb root` is not available. `adb shell` works without setting up a key.
- On the first boot, Android may briefly show a "Limited connection" notice while the network starts.
- Video apps that require Widevine DRM cannot play protected content.

## Reporting a problem

Click **Error Report** in the launcher. It saves a zip file with the instance's settings and logs to your Downloads
folder; nothing is sent automatically. Open an [issue on GitHub](https://github.com/NKS1608/kiln-android-emulator/issues)
and attach the file, with a description of what you did and what you expected.

## Supporting Kiln

Kiln is free, and donations fund its development. They go first to more Macs for testing, then to an Apple Developer
account so Kiln can be notarized, and then to support for more Android versions. Donating is optional and does not
unlock any features.

> **[Support Kiln on Ko-fi](https://ko-fi.com/nks1608)**. You can also find **Support Kiln…** in the app's Kiln menu.

## Licensing

Kiln (the launcher, the player and `kilnctl`) is proprietary software. It includes open-source components under their
own licenses:

- QEMU and Kiln's virtual devices, under GPL-2.0-or-later, and the Linux kernel modules of the Android image. Their
  source code is in the [`source/`](source/) folder of this repository, with build instructions in `BUILDING.txt`, and
  is also available on request under the written offer in `OFFER.txt`.
- Components under permissive licenses, listed in `THIRD-PARTY-NOTICES.txt`.

`LICENSE.txt` describes how each part is licensed.

Android and Google Play are trademarks of Google LLC. The Android robot is reproduced or modified from work created and
shared by Google and used according to terms described in the Creative Commons 3.0 Attribution License. Kiln is not
affiliated with or endorsed by Google, the LineageOS project, the android-lineage-qemu project, MindTheGapps, NikGApps,
any mobile operator or any device maker named here.

## Release files

| File | Contents |
| --- | --- |
| `Kiln-0.1.2.dmg` | The app, including the Android 16 image |
| `SHA256SUMS.txt` | Checksum of the disk image |
| `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt`, `OFFER.txt` | Licensing |
| `source/` (in this repository) | Source code of the open-source components |
