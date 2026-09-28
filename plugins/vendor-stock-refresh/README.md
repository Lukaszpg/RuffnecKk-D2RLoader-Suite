# Vendor Stock Refresh 2.1.3

Refresh normal vendor stock without leaving the Trade screen. This update places
its native refresh button in a centered stone-and-gold frame below the gold display. Panel revision 2 uses a continuous footer and keeps all three native buttons at 116 by 116 pixels, without a duplicate gold border.
Gambling keeps the original refresh behavior.

## Panel download

**[Download the 2.1.3 panel-assets ZIP](https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite/releases/download/vendor-stock-refresh-panel-v2.1.3/RuffnecKk-vendor-stock-refresh-panel-v2.1.3.zip)**

Use this artwork with Vendor Stock Refresh **2.1.3** from D2RLoader Hub.
The panel download contains the two vendor layouts and HD/low-quality sprites;
it does not contain the plugin DLL. Without these assets, the DLL uses its compact
button fallback.

1. Close the game and back up the vendor layouts you are replacing.
2. Extract the ZIP into `<Game>/mods/<Mod>/<Mod>.mpq/`, preserving its `data/` folder.
   For BKVince, use `<Game>/mods/BKVince/BKVince.mpq/`.
3. Install the 2.1.3 plugin in the global or selected mod's `d2rloader/plugins/`
   folder. Preserve your existing `ruffneckk-vendor-stock-refresh.toml`.
4. Launch that mod through D2RLoader and open a vendor's Trade panel.

Mods with customized vendor layouts must merge these layout changes rather than
blindly replace their existing layouts. Panel revision 1 was installed in BKVince. Revision 2 has passed offline asset and layout checks; its in-game rendering remains unverified.
The full framed layout targets keyboard/mouse; controller mode uses the compact
fallback. The frame is hidden and native gold/button positions restored in gambling.

Panel ZIP SHA-256:
`9CF2471B09A6CE51F0F60220C764BD16CF246780B84D64780B2BB5879E8223BC`.

## Compatibility and verification

The exact tested combination is D2RLoader **1.3.1-beta** with Battle.net D2R
**3.3.93847**. Release build, five automated tests, installed hashes and plugin
initialization passed; D2R completed startup 24/24. The final framed appearance,
hover/click alignment, gambling transitions, controller input and texture-quality
switching still require recorded in-game validation. Persistence, multiplayer,
Steam and CrossOver are not qualified for this update.

The test stack reported independent load failures for Advanced Item Tooltips
(mod-local), Doll Explosion and Scripted AI. This is not full-stack qualification.

## Source and assets

This directory contains the 2.1.3 source, tests, layout inputs, generated artwork
and runtime sprites. Build through the Suite CMake project. The component registers
five tests with names beginning `ruffneckk.vendor-stock-refresh`.

`node tools/build-layouts.cjs` regenerates the layouts. The artwork encoder
`tools/build-panel-assets.cjs` requires Node.js and `sharp`; the committed sprite
files do not require regeneration to build the DLL. Set `VENDOR_PANEL_REFERENCE_ROOT`
to a locally extracted native `hd/global/ui/panel` directory only when generating
preview composites. Native background and button sprites are not bundled here.

To roll back, close the game, restore the previous DLL and vendor layouts, and
restore or remove the two namespaced frame sprites according to your backup.
Preserve your configuration and unrelated mod files.
