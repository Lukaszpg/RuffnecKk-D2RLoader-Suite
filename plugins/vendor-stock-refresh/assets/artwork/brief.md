# Approved panel artwork

Generated with the built-in `image_gen` tool on 27 September 2026. Input 1 was the
native VendorForge_BG footer, locally extracted from the user's installed CASC and
cropped at `(0,1210,1162,297)`. Input 2 was Vincent's attached framed-button reference
(`codex-clipboard-2376fa10-0760-44ac-8433-47820aadb932.png`). The native extraction
receipt remains under the component's ignored `testing/runs/panel-reference-20260927`.

## Generation prompt

Use case: precise UI artwork edit for an actual Diablo II Resurrected game asset,
NOT a screenshot mockup. IMAGE 1 is the exact original footer background to edit.
IMAGE 2 is the user's desired central architectural design reference only;
disregard its white page, Edit/share badges, all text, coin icon, numerals, repair
buttons and refresh arrow. Output only the clean background artwork in the same
ultra-wide rectangular composition as image 1. Keep the original side grilles,
repair areas, outer border and top grid edge exactly unchanged. Modify ONLY the
central stonework between the two grilles. Move the empty rectangular gold display
frame upward, and build a substantial centered downward projecting dark stone
recess for a square refresh button beneath it, connected to the gold field with
symmetrical curved stone shoulders and restrained antique gold scrollwork as in
image 2. This recessed housing must look carved into the original panel. It should
touch the upper edge of the lower decorative trim without covering the bottommost
continuous outer border. Match the weathered charcoal stone, beveled silver-gray
edges and dull antique-gold inlays of the input precisely. No icons or text
anywhere: the top gold-display field and the lower button socket must both be EMPTY
dark areas because real game widgets will be drawn over them. No painted refresh
arrow and no painted button; just its inset housing. Geometry referenced to
original image 1's 1162x297 pixel canvas: unchanged centerline x578. Upper gold frame
outer bounds approximately x390..770 and y22..126; empty interior roughly
x418..744,y42..102. Lower socket has empty interior x520..636,y142..258 (square),
enclosing dark stone surround extending to x505..651 and bottom y276. Elegant
curved shoulders connect the underside of the gold frame to this socket at left
and right. Preserve all pixels outside x365..795 as closely as possible. Avoid any
modern flat graphics, glow, added text, extra slots, or broad redesign. This is raw
UI background production artwork.

## Export

The generator returned a 2170-by-725 RGB image with black letterboxing. The exact
export recipe is `../../tools/build-panel-assets.cjs`: crop letterboxing, normalize
the footer, extract the central artwork, map it into the layout's 416-by-253 insert,
and feather its outer ten pixels. Only that center insert is used in the game;
generated side grilles and borders are discarded. The existing native refresh
sprite is rendered over the empty socket by the game. The recipe encodes high and
low quality SpA1 files and records their dimensions and hashes in `../manifest.json`.

## Panel revision 2

Generated from the original empty footer and Vincent's desired reference. The entire 1162x297 footer replaces the small feathered center overlay, creating continuous horizontal rails. The plain stone socket has no duplicated gold button border. All native button frames remain 116x116. The generated source is generated-footer-r2.png; export crops its letterboxing at (0,70,2170,540), then scales to the native footer dimensions. Preview composites use original native sprites and are not game captures.

## Panel revision 3 - fixed native frame boundaries

The in-game revision 2 screenshot exposed displaced inventory-grid corners. The export now caps overlay alpha in native coordinates: left 80, right 1082, top 26 and bottom 280, with an eight-pixel inward transition. Both HD and lowend enforce this mask after resizing; tests inspect every protected pixel. The previous artwork geometry is retained inside the mask rather than relying on generated outer borders. A generated cutout proposal was inspected but not used because it changed registration. Previews now include 110 pixels of native inventory grid above the footer. Native button rectangles remain 116x116.
