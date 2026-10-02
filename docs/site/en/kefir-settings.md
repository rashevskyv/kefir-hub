# [[Kefir Settings]]

Switches for Kefir and Atmosphère that change files on the memory card. Most of them reboot the console.

**Where:** [[Tools]] → [[Kefir Settings]]

<!-- shot: kefir-settings-list | Kefir Settings list: Overclock status, 40MB Memory, USB 3.0, 8GB DRAM status, Translate Interface, each with On/Off -->

## Change a setting

1. Select the setting and press **A**.
2. A confirmation box explains what happens. Hold **A** until the bar fills (half a second; three seconds for [[8GB DRAM status]]). Press **B** to cancel.
3. Kefir Hub changes the files and reboots the console right away (except [[USB 3.0]], which asks).

<!-- shot: kefir-settings-hold-confirm | Hold-to-confirm box for a Kefir setting, "Hold A to continue" with progress bar -->

The value on the right side of each row is read from the memory card each time you open the menu,
so it shows the real state, not what you last chose.

## Settings

| Setting | What it does | Shown as On when |
|---|---|---|
| [[Overclock status]] | Turns the Kefir overclock files on or off. Off removes the sys-clk module, its overlay and the overclock kips, and keeps a backup of the sys-clk config in `/config/oc_bkp`. On copies them back from `/config/oc`. Reboots. | `/atmosphere/kips/kefir.kip` exists |
| [[40MB Memory]] | Toggles the 40MB applet memory patch (`force_40mb_applet` in Atmosphère's `system_settings.ini`). Reboots. <!-- TODO(verify): what the 40MB applet patch is for, in user terms --> | the patch is set |
| [[USB 3.0]] | Force-enables USB 3.0 in Atmosphère. Turning it on warns that it can cause crashes, instability or problems with some USB devices (hold **A** to confirm). The change takes effect only after a reboot; Kefir Hub asks [[Later]] or [[Reboot]]. | the setting is not explicitly off |
| [[Redirect Emunand saves to SD]] | Experimental. Stores emuMMC saves on the memory card. Shown only when emuMMC is enabled. Turning it off also deletes `/config/redirect.bin`. Reboots. | the setting is on |
| [[8GB DRAM status]] | Only for consoles with physically soldered 8GB RAM. Reboots into TegraExplorer to apply or remove the 8GB configuration. | `/tegraexplorer/scripts/Remove_8GB-RAM_config.te` exists <!-- TODO(verify): this file looks like it ships with Kefir; does the status really reflect whether the 8GB config is active? --> |
| [[Translate Interface]] | Opens the system translation tools. See below. | — |

<!-- TODO(verify): Kefir's out-of-the-box value for each switch -->

!!! warning
    [[Redirect Emunand saves to SD]] changes where emuMMC saves are read from. Saves can look missing until you turn it off again.

!!! warning
    [[8GB DRAM status]] is only for consoles with 8GB RAM soldered on the board. Any other console will not boot correctly.
    To undo it if the console does not boot: in hekate open **Payloads** → **TegraExplorer** and run `Remove_8GB-RAM_config.te`.

## Translate the system interface

[[Translate Interface]] replaces one of the console's own system languages (Home menu, System Settings and other
system screens) with a community translation. It does not change the Kefir Hub language.

1. Open [[Kefir Settings]] → [[Translate Interface]].
2. Select [[Load translations]] and hold **A** to download the list for your firmware. Later the item is called [[Refresh translations]].
3. Select a translation and press **A**.
4. Choose the system language to replace ([[Replace language]], or [[Select language to replace]] when none of the choices matches your console's language and region).
5. If the translation was made for a different firmware, a warning says some text may be missing or wrong. Choose [[Continue]] or [[Cancel]].
6. Hold **A** to confirm. The translation is installed and the console reboots.
7. After the reboot, set the replaced language in the console's System Settings, if it is not already active.
   <!-- TODO(verify): does the user need to switch the system language manually after install? -->

<!-- shot: kefir-settings-translate | Translate Interface list: Refresh translations, Remove installed translation, one translation entry -->

To go back to the original text, select [[Remove installed translation]] and hold **A**. The console reboots.

If no translation exists for your firmware, the first row shows your firmware and "Unsupported"; only
[[Remove installed translation]] is available.

!!! tip
    Every firmware install from the [Updater](updater.md) removes installed translations. Install the translation again afterwards.

## Problems

**"Failed to apply Kefir setting".** A file could not be written. Check that the memory card is not full or write-protected, then try again.

**The console does not boot after turning on [[8GB DRAM status]].** Run `Remove_8GB-RAM_config.te` from hekate → **Payloads** → **TegraExplorer**.

**Saves are missing on emuMMC.** If you turned on [[Redirect Emunand saves to SD]], turn it off again.
