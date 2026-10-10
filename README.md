# ReadyOrNot Control Deck

This README describes the options currently exposed by the native Control Deck.
Gameplay-changing features should only be used offline or in a private session
where the host and all participants have explicitly agreed. Multiplayer actions
are subject to server authority; this tool does not grant client users host
permissions.

## Opening and navigating the menu

- The menu opens when the module starts. The default toggle key is **Insert**.
- The default unload key is **End**. Both keys can be changed in **Misc**.
- Select a tab to open its settings. Scroll long pages with the mouse wheel or
  the vertical scrollbar. Resize the window if you need more visible space.
- Click a switch to enable or disable an option. Drag sliders to adjust values.
- Hover over a control for its tooltip.
- The status at the top reports **HOST / AUTHORITY**, **CLIENT**, or
  **WAITING FOR SESSION**. A displayed role is informational; it does not
  guarantee that a particular game action is allowed.

## Player tab

- **God Mode** — requests player damage protection where supported by the
  current session.
- **Aimbot**, **Silent Aim**, and **ESP** — enable their respective systems;
  their settings are on the matching tabs.
- **Enable Speed** and **Player Speed** — enable and configure the movement
  speed option.
- **Player FOV** — adjusts the camera field of view.
- **Instant MultiTool** — reduces multitool use time where supported.
- **Give All Achievements** — requests achievement changes. Game and session
  support may vary; this is not a substitute for legitimately earning
  achievements.

## Weapon tab

- **Infinite Ammo** — requests infinite ammunition for the equipped weapon.
- **Multi Fire** — configures the multi-fire value; zero disables additional
  shots.
- **Keep No Recoil**, **Keep No Spread**, **Keep Auto Fire**,
  **Keep Penetration**, **Keep Insta Kill**, and **Keep High Fire Rate** —
  request that the corresponding weapon setting be reapplied to newly equipped
  weapons.
- **Anti Sway** — reduces weapon sway while aiming.
- **Shoot From Reticle** — aligns shot direction with the screen reticle.
- **Add Magazine** — requests an additional magazine for the current weapon.
- **Trigger Bot** — enables automatic firing when its configured conditions
  are met. Related settings and the activation key are under **Misc**.

## World tab

- **Kill/Surrender/Arrest All Suspects** and
  **Kill/Surrender/Arrest All Civilians** — request the named action for the
  selected group in the current level.
- **Collect All Evidence** — requests collection of level evidence.
- **Auto Win** — requests mission completion.
- **Unlock All Doors** — requests that doors in the current level be unlocked.
- **Bullet Time** and **Bullet Time Speed** — enable and adjust slowed time
  where supported.

These actions can alter a mission and may be unavailable or rejected when the
session does not grant the required authority.

## Aimbot tab

Contains controls for **Aimbot FOV**, line-of-sight requirement, target
categories (civilians, dead, arrested, surrendered, or all), minimum and maximum
distance, smoothing and its strength, movement prediction and its multiplier,
target lock, visual arrow and FOV indicators, target bone, and the activation
hotkey. **Require Hotkey** controls whether the selected key must be held.

## ESP tab

Contains switches for team display, boxes, traps, enemy distance, bones,
line-of-sight requirement, and objectives. The color controls choose suspect,
civilian, dead, team, and arrested colors. **Bone Opacity** adjusts bone-line
opacity and the related character-color opacity.

## Silent Aim tab

Contains controls for FOV, target categories (civilians, dead, surrendered,
arrested, or all), line-of-sight requirement, FOV and snap-line indicators and
their thickness, hit chance, and target bone.

## Misc tab

- **Reticle**, **Reticle Color**, **Reticle Opacity**, **Reticle Size**,
  **Cross Reticle**, **Reticle Only While Throwing**, and X/Y position
  configure the local reticle overlay.
- **Trigger Bot Targets Civilians**, **Trigger Bot Uses Silent Aim**,
  **Trigger Bot Hold Key**, and **Trigger Bot Key** configure triggerbot
  behavior and activation.
- **Show Enabled Options** toggles the on-screen enabled-options list.
- **List Players** enables player-list collection for the Players tab.
- **Stream Proof** requests capture exclusion where supported by Windows and
  the capture method; behavior is not guaranteed.
- **Auto Save Settings** periodically saves configuration.
- **Save Enabled Cheats** includes enabled feature flags when saving.
- **Save Debug Output** writes debug output to the configured log.
- **Menu Toggle Key**, **Uninject Key**, and **ESP Key** configure those
  bindings. Changing the menu or unload key attempts to register the new
  hotkey; if registration fails, the previous key is restored.
- **Save Settings** and **Load Settings** manually save or load configuration.

## Debug tab

- **Debug** enables verbose diagnostics and can significantly affect
  performance.
- **Secret Features** enables experimental behavior and may be unstable.
- **Function name filter** and **Object name filter** limit matching debug
  entries.
- **Print Actors** writes discovered actor information to debug output.

## Players tab

Shows the number of connected players and up to the first 10 listed entries.
Each entry can display God Mode or Infinite Ammo status. **Copy Name** copies
the displayed name to the local clipboard.

The God Mode, Infinite Ammo, Teleport To Self, and Speed actions are
authority-dependent. They may be disabled or unavailable to clients and are
not intended to bypass host permissions.

## Saving and limitations

Use **Misc → Save Settings** to save manually, or enable **Auto Save Settings**
for periodic saves. **Load Settings** restores the saved configuration.
Configuration does not make unsupported game actions work: availability and
results depend on game state, game version, and session permissions. Verify
behavior in an offline or explicitly authorized environment.

