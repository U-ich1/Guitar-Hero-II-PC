# Performance

For smoother play, compare settings using the same song and venue.

1. Start at 1× internal resolution.
2. Enable **No audience** in Graphics or F5 if busy venues cause drops.
3. Compare Display VSync on and off.
4. Try the display FPS limit at 60, or 0 for no presentation limit.
5. Increase image quality gradually once frame delivery is consistent.

**No audience** hides crowd rendering while keeping crowd sound and gameplay intact. Crowd animation and loaded objects remain present, so the improvement varies by scene.

The game runs its original 60 Hz simulation. A higher display setting does not make song logic run faster. DLSS and FSR are not available.

## Reporting a problem

After closing the game normally, use **Open logs** in the launcher. Include the song, venue, resolution, VSync and FPS limit when reporting stutters. Mention whether sound is affected or only the visuals.

For a performance report, run `python scripts/analyze_performance.py` from the project folder. The report is saved to `build/performance-report.json`. Averages alone can hide short frame drops, so compare worst intervals as well.
