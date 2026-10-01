# User Profiles & Account Linking

Kefir Hub includes an integrated User Profile and Account Management utility located in **Tools -> Tools -> Users**. It enables full profile lifecycle management, custom avatar assignment, offline Nintendo Account linking, and portable backup creation.

---

## 1. Profile Creation & Customization

- **Horizon User Creator Applet:** Create new local user profiles directly via Horizon OS's native creation applet (`pselShowUserCreator`). Cancelling the creation applet at any stage exits cleanly without error notifications.
- **Custom Profile Avatars:** Assign custom avatar images from:
  - Built-in avatar presets
  - Local images on SD (`/config/kefir/avatars/` or file picker)
  - Interactive SteamGridDB search
- **Reboot Prompt on Avatar Change:** Because Horizon OS's HOME menu (`qlaunch`) and system applets cache user avatars in memory at boot time, changing an avatar prompts the user to either **Reboot now** (recommended for immediate system-wide reflection) or **Reboot later**.

---

## 2. RomFS Donor Offline Nintendo Account Linking

Many Nintendo Switch games require a linked Nintendo Account to enable features like local wireless play, LAN play, or certain game modes. Kefir Hub provides built-in offline account linking:

- **Integrated RomFS Donor Pool:** Sphaira embeds verified Nintendo Account donor templates directly in RomFS (`romfs:/kefir/donor/`), eliminating the need to install third-party homebrew utilities (such as Linkalho).
- **Official vs Fake Status Classification:**
  - Accurately inspects the console's account database (`8000000000000010`) using Horizon's native BaaS Administrator IPC (`GetBaasAccountAdministrator` and `IsLinkedWithNintendoAccount`); profile link status and NAS ID are queried the same way.
  - Clearly differentiates between **Official** Nintendo Accounts and **Fake / Offline** linked accounts, displaying distinct color-coded status badges in the UI.
- **Safe Linking & Emergency Rollbacks:**
  - Before modifying account system save `8000000000000010`, Sphaira takes an emergency rollback snapshot to protect against corruption or boot issues.
  - Official Nintendo Accounts are strictly protected from accidental overwrite unless explicitly confirmed by the user.
- **Unlink Account:** Easily remove fake/offline Nintendo Account linkage from any profile directly via the **Unlink** action in the options sidebar without deleting the profile or its game saves.
- **Safe Profile Deletion:** Deleting a user profile removes its local Horizon registration while safely offering to back up or preserve associated game save data.

---

## 3. Portable User Backups

- Export individual user profile definitions (UID, nickname, avatar, linkage metadata) to standalone, human-readable portable backup archives stored in `/config/kefir/user_backups/`.
- Backups can be shared wirelessly with other consoles or restored cleanly onto newly formatted NANDs.
- **One-Time TegraExplorer Restore Notification:** When restoring user profiles & play hours packs through TegraExplorer, if restoration does not complete, a concise status notification is presented once upon returning to Kefir Hub, cleanly persisting the applied state so it does not repeat on subsequent launches.
