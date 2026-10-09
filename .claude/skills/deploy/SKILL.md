---
name: deploy
description: >
  Ship Kefir Hub to GitHub as a new release: English changelog, git push, tag without a leading v,
  gh release with kefir-hub.nro. Not nxlink, not a console send.
  Triggers: /deploy, "деплой", "deploy", "GitHub release", "залий на гітхаб", "зроби реліз".
---

Canonical procedure lives in `.agents/skills/deploy/SKILL.md` (shared by Codex, Gemini, Grok and Claude).
Read that file and follow it exactly. Console nxlink is a different request: this skill never sends the NRO to the Switch.
