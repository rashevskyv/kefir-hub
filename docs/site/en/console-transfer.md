# [[Console Transfer]]

Move profiles, play hours, save backups, screenshots, homebrew and other files from one console to another over
your home network. The sending console shares its files; the receiving console (or a PC or phone browser)
downloads them.

**Where:** on the sending console [[Tools]] → [[Console Transfer]].
For profiles and play hours, the receiving console uses [[Tools]] → [[Tools]] → [[Users]].

<!-- shot: console-transfer-menu | Console Transfer screen with all seven share items -->

## What you can move
| What | Sending console | Receiving console |
|---|---|---|
| All profiles and their play hours | [[Share Profiles & Play Hours]] | [[Users]] → [[Restore from another console]] or [[Receive from another console]] |
| Save backups | [[Share Save Backups]] | Download the files, then restore them in [[Saves]] |
| Screenshots and videos | [[Share Screenshots & Videos]] | Download in a browser |
| Homebrew apps (`/switch`) | [[Share switch Folder]] | Download in a browser |
| The whole memory card | [[Share Entire microSD]] | Download in a browser |
| One folder you choose | [[Choose Folder...]] | Download in a browser |

What is **not** moved:

- Installed games. Console Transfer shares files, not installed titles. Install the games on the new console
  from your files, see [Install games](install/index.md).
- Game saves. Copying them with the profiles would take too long. Back them up in [[Saves]] and move the
  backups with [[Share Save Backups]], see [Move save backups](#move-save-backups).
- [[Share User Backups]] shares the profile backups in `/config/kefir/account_backups/`. Restoring them on the
  receiving console is not available yet.

## Before you start
- Both consoles are on the same local network (the same Wi-Fi, not a guest network).
- Both consoles run Kefir with Kefir Hub.
- To restore profiles and play hours you also need hekate and TegraExplorer. Kefir Hub copies its own
  `TegraExplorer.bin` to `/bootloader/payloads/` when needed.
- Make a SYSTEM backup in hekate on the receiving console before restoring profiles.

## Start sharing on the sending console
1. Open [[Tools]] → [[Console Transfer]].
2. Choose what to share and press **A**. If the console is offline, Kefir Hub asks you to connect first.
3. The server screen opens with the address (for example `http://192.168.1.20`) and a QR code.
   Write down the IP address: you type it on the receiving console.
4. Keep this screen open until the receiving side has finished. Press **B** to stop sharing.

If a folder that should be shared does not exist (for example, you have no save backups yet), Kefir Hub shows
"Failed to start folder server". If a server is already running, Kefir Hub switches it to the new folders and
shows a short notice with the address.

The server stops by itself when the console loses its network or its IP address changes.

<!-- shot: console-transfer-server-screen | Server screen titled Console Transfer: address, QR code, "Press B to Stop Server" -->

## Move profiles and play hours
This copies every profile on the sending console, with the same user IDs, and optionally its play hours.
Game saves are not copied: move them separately, see [Move save backups](#move-save-backups).
Because the user IDs stay the same, save backups from the old console restore to the same profiles on the new one.

!!! warning
    The receiving console loses all its own profiles: they are replaced by the ones from the sending console.
    With play hours, its play history is replaced too. Make a SYSTEM backup in hekate first.
    <!-- TODO(verify): what happens to game saves of profiles that existed only on the receiving console? If they become unreachable, tell the reader to back them up first. -->

On the sending console:

1. Open [[Users]], press **A**, choose [[Backup profiles & play hours]], confirm with [[Backup]].
   If the console restarts into TegraExplorer, let it finish; then start Kefir and open Kefir Hub again.
   See [Back up](users.md#back-up).
2. Open [[Tools]] → [[Console Transfer]] → [[Share Profiles & Play Hours]].
   You can also use [[Send to another console]] in [[Manage Backups]].

On the receiving console:

3. Open [[Users]], press **A**, choose [[Restore from another console]].
   To only download the backup and restore later, choose [[Receive from another console]] instead.
4. Type the sending console's IP address. The keyboard already holds the first part of your network address.
   You can add a port (`192.168.1.20:8080`); without one, Kefir Hub tries the usual ports itself.
5. Kefir Hub tests the connection, then lists the backups on the sending console. Choose one.
   With [[Receive from another console]] and several backups, the first line downloads all of them.
6. The backup is saved to `/config/kefir/nand_transfer/`. With [[Restore from another console]], Kefir Hub then asks
   what to restore: [[Profiles only]] or [[Profiles + play hours]].
7. Press [[Launch TegraExplorer]]. The console restarts into TegraExplorer, writes the backup, and returns to
   hekate. Do not touch the console while it works.
8. Start Kefir and open Kefir Hub. It shows whether the restore finished.

<!-- shot: console-transfer-ip-entry | Keyboard "Enter sending console IP address" with the network prefix filled in -->
<!-- shot: console-transfer-remote-list | List of backups on the sending console, first line receives all -->
<!-- shot: console-transfer-te-confirm | "Ready to restore profiles & play hours through TegraExplorer" dialog with Launch TegraExplorer -->

!!! tip
    Without a network: copy the `.kefir-nand.zip` file from `/config/kefir/nand_transfer/` on the sending console's
    memory card to the same folder on the receiving one, then use [[Restore profiles & play hours]] in [[Users]].

## Move save backups
1. On the sending console, back up the saves you need in [[Saves]]. See [Saves](saves.md).
2. Open [[Tools]] → [[Console Transfer]] → [[Share Save Backups]]. Kefir Hub shares your save backup folders:
   `/dumps`, the DBI save folders and any extra backup folders you added in [[Saves]].
3. Get the files onto the receiving console's memory card, either way:
    - open the address in a PC or phone browser, download the backups, then upload them to the receiving
      console (see [Sharing](sharing.md));
    - on the receiving console, open [[File Browser]] → [[Sources]] → [[Add network location]], choose HTTP and
      the sending console's address, then copy the backups to the same folder (for example `/dumps`).
      <!-- TODO(verify): confirm that an HTTP network location pointing at another Kefir Hub opens and copies correctly. -->
4. On the receiving console, restore the saves in [[Saves]]. See [Saves](saves.md#restore-a-save).

## Move screenshots, homebrew and other files
1. On the sending console, open [[Tools]] → [[Console Transfer]] and choose [[Share Screenshots & Videos]],
   [[Share switch Folder]], [[Share Entire microSD]] or [[Choose Folder...]].
   With [[Choose Folder...]], go into the folder and select [[Select current folder]].
2. Open the shown address in a browser on a PC or phone and download what you need. See [Sharing](sharing.md).

On emuMMC, [[Share Screenshots & Videos]] shares the album of the emuMMC you booted.

## Undo a profiles restore
Before writing, Kefir Hub and TegraExplorer save a copy of the receiving console's profile and play-hours data on
the memory card. If the console does not boot after a restore:

1. Enter hekate, open **Payloads** → **TegraExplorer**.
2. Run the script `Undo_restore_if_wont_boot.te`. TegraExplorer is controlled with the power and volume buttons.
   This puts the old profiles back.
3. If the undo copy is missing or the console still does not boot, restore your SYSTEM backup in hekate.

## Problems
**"Could not connect to the remote console".** Check that both consoles are on the same network and that the
sending console still shows the server screen. Check the IP address on that screen.

**"No profiles & play hours backups found on the sending console".** The sending console shares something else,
or has no backup yet. Make a backup there and start [[Share Profiles & Play Hours]].

**"Failed to start folder server".** The folders to share do not exist yet on the sending console.

**"Could not start TegraExplorer".** Copy `TegraExplorer.bin` to `/bootloader/payloads/` and try again.

**"TegraExplorer did not finish restoring profiles & play hours".** The restore did not complete. If the console
boots, run the restore again. If not, see [Undo a profiles restore](#undo-a-profiles-restore).

**The server stops during the transfer.** The sending console lost its network or got a new IP address.
Reconnect, start sharing again, and use the new address.
