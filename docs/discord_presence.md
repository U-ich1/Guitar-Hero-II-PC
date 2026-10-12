# Discord Rich Presence

The game uses application ID `1559050915195912242`. No public key, bot token,
client secret or account login is stored or required by this integration.
Discord's desktop application must be open, with activity sharing enabled.

The launcher **Settings > Graphics > Discord Rich Presence** checkbox controls
`gh2_discord_presence` in the shared settings. It is enabled by default.
Turning it off clears the activity. Venue cards appear during gameplay, pause
and results; the original game icon appears in menus. `game get_venue` supplies
the loaded venue, independently of the selected song or mode.

Upload the PNGs in `build/discord-assets` to this Discord application's
**Rich Presence > Art Assets**, using each filename without `.png` as the key:
`gh2_logo`, `venue_battle`, `venue_small1`, `venue_small2`, `venue_big`,
`venue_theatre`, `venue_fest`, `venue_arena`, and `venue_stone`.
The same artwork is included in `build/discord-assets.zip` for convenience.
Missing uploads do not affect the text presence. Unknown venues use the
game icon. Image hover text uses the venue's original English localization.

`scripts/extract_discord_venues.py` (requires Pillow) exports the original
512x512 venue-card textures from the local dump and preserves the PNG title
icon embedded in the ICO. All eight decoded cards were visually inspected,
and exported PNG pixels were checked by round trip. They are excluded from
the source-only release; no artwork is uploaded by this script.

Presence shows Career, Quick Play, Practice, Tutorial, Co-op, Face-Off or
Pro Face-Off, the active song title, and the first player's difficulty.
Pause and results screens are labelled. Menu transitions clear the song and
difficulty; attract mode is reported as menus. Multiplayer currently shows
player one's difficulty. Song titles come from the user's own songs DTB;
unknown song IDs use their identifier as a fallback.

`prepare_pc_menu.py` builds the read-only snapshot additions into the existing
supplemental archive, retaining Options/Quit and native cheat functionality.
Original ARK data remains unchanged. Screen polls publish five script variables
using the original game accessors; translated DataVariable lookup observes their
nodes, including the venue. The guest swap samples them at most twice per second without waiting for
the IPC worker. No guest memory is read by that worker.

The worker uses [Discord's documented local RPC over IPC](https://docs.discord.com/developers/topics/rpc),
trying pipes 0-9, handshaking the public application ID, handling ping/pong and
command acknowledgements, and reconnecting after errors. I/O waits are bounded,
cancelable, and confined to the worker. It publishes only mode, song, difficulty
and status, with the game PID; it does not publish saves, usernames or scores.

`tests/check_discord_presence.py` uses synthetic guest nodes and a private mock
named pipe to test field formatting, menu clearing, pause/results, UTF-8/JSON,
handshake, ping/pong, null-event acknowledgements and clearing. It never launches
GH2 or talks to Discord. Native scripts round-trip and supplemental generation
is byte-idempotent. Existing menu/cheat script tests and the hidden launcher UI
test pass. Live screen transitions and Discord profile display still require
observation in a normal play session.
