# Installing Septima and Vista Rooms

Download the latest release for your system from the project's **Releases**
page on GitHub:

| System | File | Formats |
| --- | --- | --- |
| macOS 11 or later (Apple Silicon and Intel) | `Rereverb-x.y.z.pkg` (installer) or `Rereverb-x.y.z-macOS.zip` | AU, VST3 |
| Windows 10/11, 64-bit | `Rereverb-x.y.z-Windows-Setup.exe` (installer) or `Rereverb-x.y.z-Windows.zip` | VST3 |

The plugins are free and aren't code-signed, so both systems ask you to
approve them once. This is normal for free plugins that don't come from a
paid developer account.

## macOS

1. Double-click `Rereverb-x.y.z.pkg`. macOS will say it "can't be opened
   because Apple cannot check it for malicious software". Click **Done** or
   **OK**.
2. Open **System Settings → Privacy & Security** and scroll down to the
   *Security* section. You'll see a message about `Rereverb-x.y.z.pkg`. Click
   **Open Anyway** and enter your password.
3. Follow the installer. It installs:
   - `/Library/Audio/Plug-Ins/Components/Septima.component` and `Vista Rooms.component` (AU)
   - `/Library/Audio/Plug-Ins/VST3/Septima.vst3` and `Vista Rooms.vst3` (VST3)
4. Restart your DAW. Logic Pro rescans Audio Units when it starts. If a
   plugin is missing, open **Logic Pro → Settings → Plug-in Manager**,
   select it and click **Reset & Rescan Selection**.

**Using the zip instead:** copy the `.component` files to
`~/Library/Audio/Plug-Ins/Components/` and the `.vst3` files to
`~/Library/Audio/Plug-Ins/VST3/`. Then remove the download quarantine flag in
Terminal:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Septima.component \
  ~/Library/Audio/Plug-Ins/Components/"Vista Rooms.component" \
  ~/Library/Audio/Plug-Ins/VST3/Septima.vst3 ~/Library/Audio/Plug-Ins/VST3/"Vista Rooms.vst3"
```

## Windows

1. Run `Rereverb-x.y.z-Windows-Setup.exe`. If **Windows protected your PC**
   (SmartScreen) appears, click **More info → Run anyway**.
2. Choose the plugins you want. The installer copies them to
   `C:\Program Files\Common Files\VST3\`, the standard VST3 folder every DAW
   scans.
3. Rescan plugins in your DAW. In Ableton, use **Options → Preferences →
   Plug-ins → Rescan**. In FL Studio, use **Options → Manage plugins → Find
   plugins**. In Cubase, the scan happens automatically on startup.

**Using the zip instead:** copy both `.vst3` folders into
`C:\Program Files\Common Files\VST3\`.

## Septima's impulse responses (required)

Septima is a convolution reverb and needs the free impulse response library
from Samplicity. Its licence doesn't allow it to be redistributed, so you
download it yourself, once.

1. Go to <https://samplicity.com/downloads/> and download
   **Samplicity – Bricasti IRs version 2023-10.zip** (about 960 MB).
2. Unzip it.
3. Use either option:
   - **Option A (simplest):** open Septima, click **IR Library…** and choose
     the unzipped `Samplicity - Bricasti IRs version 2023-10` folder. Picking
     the top-level folder is fine: Septima searches the sub-folders and keeps
     one 48 kHz copy of each of the 134 programs. The choice is remembered
     for every instance and every DAW.
   - **Option B:** copy the WAV files from the
     `…quad channel files, 48 Khz` sub-folder into Septima's default folder,
     which it checks automatically:
     - macOS: `/Library/Application Support/Rereverb/Septima/IRs/` (all users)
       or `~/Library/Application Support/Rereverb/Septima/IRs/` (just you)
     - Windows: `C:\ProgramData\Rereverb\Septima\IRs\` (created by the
       installer) or `%APPDATA%\Rereverb\Septima\IRs\`

Until it finds the IRs, Septima shows *"No IRs found: click IR Library… and
choose your IR folder"*. If you move the folder later, choose it again with
**IR Library…**. Your sessions and presets still load, because programs are
matched by name rather than by file path.

## Where things are stored

| | macOS | Windows |
| --- | --- | --- |
| User presets | `~/Library/Audio/Presets/Rereverb/<plugin>/` | `%APPDATA%\Rereverb\Presets\<plugin>\` |
| Septima IR folder setting | `~/Library/Application Support/Rereverb/Septima.settings` | `%APPDATA%\Rereverb\Septima.settings` |

## Uninstalling

- **macOS:** delete the four plugin files listed above, plus
  `/Library/Application Support/Rereverb` if you want to remove the IRs too.
- **Windows:** use **Settings → Apps → Rereverb → Uninstall**. The uninstaller
  leaves the IR folder and your presets in place.
