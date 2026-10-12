# Discord Rich Presence

Keep Discord's desktop app open and enable activity sharing. Toggle **Discord Rich Presence** in **Launcher → Settings → Graphics**.

Presence shows your mode, song and difficulty, with venue imagery during gameplay, pause and results. Menus use the project cover image. Multiplayer shows the first player's difficulty.

Turning the option off clears the activity. No Discord login, bot token or account secret is needed by the game.

## Image setup for maintainers

The integration uses application ID `1559050915195912242`. Upload artwork to that application's **Rich Presence → Art Assets** using these keys:

`gh2_logo`, `venue_battle`, `venue_small1`, `venue_small2`, `venue_big`, `venue_theatre`, `venue_fest`, `venue_arena`, `venue_stone`.

Players using the project application do not need to upload images themselves. Missing venue art falls back to the cover image.

`scripts/extract_discord_venues.py` can export venue cards from a local dump to `build/discord-assets`. Extracted artwork is not included in source releases.
