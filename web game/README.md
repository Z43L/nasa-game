# BrainSpaceOverflow // Retro Terminal Web Game Start Menu

Welcome to the visual frontend for **BrainSpaceOverflow**, a cyberpunk terminal-themed web game!

## Features Included
1. **Authentic CRT Retro Terminal Aesthetic**:
   - Customizable phosphor glow color themes (Classic Green, Neon Amber, Cyan Matrix, Pale White).
   - Real CRT scanlines overlay and subtle screen flicker animation.
   - Monospaced retro typography (`VT323` and `Share Tech Mono`).
   - Header with live system clock, memory status, audio toggle, and theme switcher.

2. **Immersive Boot Sequence**:
   - Animated BIOS/kernel boot logs streaming on initial load.
   - Bypasable via `[ENTER]`, `[SPACE]`, or click.

3. **Retro Terminal Menu & Navigation**:
   - Large ASCII art banner (`BRAIN SPACE OVERFLOW`).
   - Interactive menu options:
     - `[1] INITIALIZE MEMORY (NEW GAME)`
     - `[2] RESTORE SNAPSHOT (LOAD GAME)`
     - `[3] SYSTEM CONFIG (SETTINGS)`
     - `[4] NEURAL ARCHIVES (CREDITS)`
     - `[5] DISCONNECT (QUIT)`
   - Full keyboard navigation (`[↑][↓]` arrows, `[1-5]` number keys, `[ENTER]` confirm, `[ESC]` back).
   - Mouse hover and click support with synthesized Web Audio API sound effects.

4. **Interactive Terminal Command Prompt**:
   - Type commands directly (`help`, `start`, `load`, `settings`, `credits`, `theme`, `audio`, `clear`, `hack`, `exit`).

## How to Run
Simply open `index.html` in any modern web browser:
- Or serve locally via python: `python3 -m http.server 8000`
