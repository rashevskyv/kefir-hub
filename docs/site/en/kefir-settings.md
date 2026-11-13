# [[Kefir Settings]]

Switches for Kefir and Atmosphère that change files on the memory card. Most of them reboot the console.

**Where:** [[Tools]] → [[Kefir Settings]]

<!-- shot: kefir-settings-list | Kefir Settings list: Overclock status, 40MB Memory, USB 3.0, 8GB DRAM status, Translate Interface, each with On/Off -->

Before launching TegraExplorer, Kefir Hub compares the memory-card payload with its bundled copy. It installs the bundled copy if the file is missing, or updates an older version. An equal or newer version on the card stays unchanged. If installation fails, Hub does not launch it.

## Change a setting

1. Select the setting and press **A**.
2. A confirmation box explains what happens. Hold **A** until the bar fills (half a second; three seconds for [[8GB DRAM status]]). Press **B** to cancel.
3. Kefir Hub changes the files and reboots the console right away (except [[USB 3.0]], which asks).

<!-- shot: kefir-settings-hold-confirm | Hold-to-confirm box for a Kefir setting, "Hold A to continue" with progress bar -->

The value on the right side of each row is read from the memory card each time you open the menu,
so it shows the real state, not what you last chose.

## Settings

| Setting | What it does | Shown as On when | Default |
|---|---|---|---|
| [[Overclock status]] | Turns the Kefir overclock files on or off. Off removes the sys-clk module, its overlay and the overclock kips, and keeps a backup of the sys-clk config in `/config/oc_bkp`. On copies them back from `/config/oc`. Reboots. | `/atmosphere/kips/kefir.kip` exists | Off |
| [[40MB Memory]] | Newer Atmosphère versions leave less memory for applets (homebrew started from the Album) and for LayeredFS game mods. This patch gives that memory back (`force_40mb_applet` in Atmosphère's `system_settings.ini`). Some games do not work properly with it on: if a game misbehaves, turn it off. Reboots. | the patch is set | Off |
| [[USB 3.0]] | Force-enables USB 3.0 in Atmosphère. Turning it on warns that it can cause crashes, instability or problems with some USB devices (hold **A** to confirm). The change takes effect only after a reboot; Kefir Hub asks [[Later]] or [[Reboot]]. | the setting is explicitly on | Off |
| [[Redirect Emunand saves to SD]] | Experimental. Stores emuMMC saves on the memory card. Shown only when emuMMC is enabled. Turning it off also deletes `/config/redirect.bin`. Reboots. | the setting is on | Off |
| [[8GB DRAM status]] | Only for consoles with physically soldered 8GB RAM. Reboots into TegraExplorer to apply or remove the 8GB configuration. If the console boots with it On, the 8GB RAM is there and works. | `/tegraexplorer/scripts/Remove_8GB-RAM_config.te` exists. A console without 8GB RAM does not boot with it on, so On shows only on a real 8GB console. | Off |
| [[Translate Interface]] | Opens the system translation tools. See below. | — | — |

[[40MB Memory]] is shown only on firmware 20.0.0 to 22.x, and on firmware 23.0.0 and later with Atmosphère below 1.12.1 (or when the Atmosphère version cannot be read). Firmware up to 19.0.1 has 40 MB itself, and Atmosphère 1.12.1 gives 40 MB on firmware 23.0.0 and later, so the switch is not needed there.

!!! warning
    [[Redirect Emunand saves to SD]] changes where emuMMC saves are read from. Saves can look missing until you turn it off again.

!!! warning
    [[8GB DRAM status]] is only for consoles with 8GB RAM soldered on the board. Any other console will not boot correctly.
    To undo it if the console does not boot: in hekate open **Payloads** → **TegraExplorer** and run the script `Remove_8GB-RAM_config.te`.

## Translate the system interface

[[Translate Interface]] replaces one of the console's own system languages (Home menu, System Settings and other
system screens) with a community translation. If Kefir Hub has the same language, it switches its own interface
to it as well, without asking, and then reboots.

After the install, Kefir Hub no longer switches its own language silently. When it has the same language, it asks [[Switch Kefir Hub to this language too?]] with the language name under it and the note [[The console restarts after this.]]. Choose [[Keep]] or [[Switch language]]; either answer restarts the console. When Kefir Hub already uses that language, or has none like it, the console restarts at once.

1. Open [[Kefir Settings]] → [[Translate Interface]].
2. Select [[Load translations]] and hold **A** to download the list for your firmware. Later the item is called [[Refresh translations]].
3. Select a translation and press **A**.
4. Choose the system language to replace ([[Replace language]], or [[Select language to replace]] when none of the choices matches your console's language and region).
5. If the translation was made for a different firmware, a warning says some text may be missing or wrong. Choose [[Continue]] or [[Cancel]].
6. Hold **A** to confirm. The translation is installed and the console reboots.
7. After the reboot, set the replaced language in the console's System Settings, if it is not already active.
   <!-- TODO(verify): does the user need to switch the system language by hand after install? (Kefir Hub's own language switches automatically.) -->

<!-- shot: kefir-settings-translate | Translate Interface list: Refresh translations, Remove installed translation, one translation entry -->

To go back to the original text, select [[Remove installed translation]] and hold **A**. The console reboots.

If no translation exists for your firmware, the first row shows your firmware and "Unsupported"; only
[[Remove installed translation]] is available.

!!! tip
    Every firmware install from the [Updater](updater.md) removes installed translations. Install the translation again afterwards.

## Nextendo

Nextendo Network gives online play to supported games on the emuMMC. The online services of these games go to the Nextendo servers instead of the Nintendo servers. Nextendo is off until you switch it on.

**Where:** [[Tools]] → [[Kefir Settings]] → [[Nextendo]]

<!-- shot: kefir-settings-nextendo | Nextendo page on the emuMMC: Nextendo on emuMMC switched on, System firmware, Server and Release rows, the Required Notice line at the bottom -->

You need internet on the emuMMC and a free Nextendo account. Create the account on the [Nextendo site](https://nextendo.network/register).

To switch Nextendo on:

1. Start Kefir on the emuMMC and open Kefir Hub.
2. Open [[Tools]] → [[Kefir Settings]] → [[Nextendo]].
3. Select [[Nextendo on emuMMC]] and hold **A**.
4. Kefir Hub writes the Nextendo files to the memory card. If a file changed, the console restarts.
5. Link the console to your Nextendo account. The [Nextendo guide in Discord](https://discord.gg/XPfeCMwnzQ) describes how.
6. Start a supported game and open its online mode.

<!-- draft
- selecting [[Nextendo on emuMMC]] while it is off first shows a box with a QR code to https://nextendo.network/register and the account steps
- the Nextendo server accepts only a Nextendo account: a profile linked with Linkalho or a Kefir Hub donor cannot play online on Nextendo
- no profile is linked: [[Continue]] goes on to the usual hold **A** switch
- some profiles are linked: [[Choose profiles]] opens the list [[Profiles for Nextendo]]; **X** ticks a profile, **Y** inverts, **A** or **B** closes the list
- after the list: [[Switch on]] switches Nextendo on, unlinks the ticked profiles and restarts the console once; game saves stay; nothing ticked = Nextendo is only switched on
- [[Cancel]] in either box changes nothing
- step 5 changes: after the restart link each profile that plays online in System Settings → Users → the profile → Link Nintendo Account; sign in with the Nextendo account (OWNER: check on a console what the sign-in screen asks for)
- unlinked profiles get the startup reminder with [[Link through Nextendo]] (see Users)
- Kefir Hub cannot yet tell a Nextendo link from a donor link
-->

<!-- shot: kefir-settings-nextendo-account | Box after selecting Nextendo on emuMMC with linked profiles: QR code, account text, Cancel and Choose profiles -->

The [[Games]] list marks every installed game that Nextendo supports with a green [[Online]] badge and the number of players online now. See [Games](games.md).

To switch Nextendo off, select [[Nextendo on emuMMC]] again and hold **A**. Kefir Hub deletes its Nextendo files; your own patches stay.

| Row | What it shows |
|---|---|
| [[Nextendo on emuMMC]] | On or off. Off by default. |
| [[Nextendo on sysNAND (Semi-stock)]] | Shown only while Semi-stock is on. Off by default. |
| [[System firmware]] | The installed firmware. The description shows the ssl ID that the Nextendo patch must match. |
| [[Server]] | The Nextendo server address in the hosts file. |
| [[Release]] | The Prelude-Nextendo release that Kefir Hub is based on. |
| [[Status]] | Shown only when Nextendo cannot work (see below). |

Kefir Hub writes these files: the hosts file `atmosphere/hosts/emummc.txt`, the `nextendo_*` folders in `atmosphere/exefs_patches` and `atmosphere/nro_patches`, and the browser certificates in `atmosphere/contents/0100000000000803`. A Kefir update keeps the `nextendo_*` folders. New Prelude-Nextendo releases come with Kefir Hub updates; no Kefir update is needed.

!!! warning
    [[Nextendo on sysNAND (Semi-stock)]] sends the Nintendo services of the original system to the Nextendo Network. The eShop and other official online features may stop working. Semi-stock has no PRODINFO protection. Leaving Semi-stock switches this option off.

**[[Status]]: [[No patch for this firmware]].** This Kefir Hub has no Nextendo patch for the installed firmware, so Nextendo is off. Update Kefir Hub; it gets the patch when Prelude-Nextendo adds one.

**[[Status]]: [[Not in use]].** The console runs neither the emuMMC nor Semi-stock. Start Kefir on the emuMMC.

**Prelude-Nextendo app.** If you set up Nextendo with the Prelude-Nextendo app, its hosts file stays while Nextendo is off in Kefir Hub. When you switch Nextendo on in Kefir Hub, Kefir Hub writes its own hosts file over it.

## Problems

**"Failed to apply Kefir setting".** A file could not be written. Check that the memory card is not full or write-protected, then try again.

**The console does not boot after turning on [[8GB DRAM status]].** Run `Remove_8GB-RAM_config.te` from hekate → **Payloads** → **TegraExplorer**.

**Saves are missing on emuMMC.** If you turned on [[Redirect Emunand saves to SD]], turn it off again.
