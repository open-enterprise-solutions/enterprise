# UI Palette

The OES Enterprise frontend uses an interior-design palette: a cool dusty-blue dominant chrome around warm-cream content surfaces, with a single warm focal accent (terracotta) for active states. The composition is inspired by classic interiors that pair powder-blue walls with a terracotta ottoman as the room's focal point — a calm field with one warm point of attention.

This document is the source of truth for the colour values used across the codebase. When you change a UI surface, look here first to pick a value that already belongs to the palette rather than introducing a new tone.

## Design principles

- **Cool chrome, warm content.** Frame elements (MDI workspace, dock, tab strip, captions, panel borders, status bar) use dusty-blue tones. Content surfaces (code editor, metadata tree, debugger lists, form-design canvas) use warm cream. The cool/warm boundary itself does most of the visual hierarchy work — no hard rules or saturated highlights needed.
- **One warm accent, used sparingly.** Terracotta `#D97757` is reserved for active states that the user must notice: editor selection, "primary action" buttons, focal property tints. It is the equivalent of the ottoman in the reference photo. If you find yourself reaching for it for ordinary surfaces, pick a chrome tier instead.
- **Borders recede.** Pane outlines use a light dusty-blue (`#A8BAC8`) — visible but not loud. Avoid neutral-blue borders against warm content; they read as a foreign tone and pull the eye.
- **Captions get gradients, surfaces stay flat.** The only deliberate vertical gradients in the UI are on AUI captions (active and inactive). All other surfaces are solid fills so the eye doesn't see "depth" where there is none.

## Tier table

The palette is organised as a tier list from lightest content to deepest chrome. Each tier has a documented role; reuse a tier instead of inventing a new colour when you add a UI element.

| Tier | Hex | Role |
|---|---|---|
| content-light | `#FDFBF5` | debugger lists (watch / locals / call-stack) — lightest tier, where the eye lands during debugging |
| content | `#FAF7F0` | code editor, metadata tree, list controls, active tab interior, syntax-helper detail view |
| shelf | `#E0D8CC` | the 4 px band directly under the tab strip — neutral warm taupe that reads as a "shelf" the active tab sits on |
| caption-inactive-top | `#D8E2EB` | inactive caption gradient top; also form-design workspace background |
| caption-inactive-bottom | `#C5D2DC` | inactive caption gradient bottom |
| chrome-light | `#C8D6DF` | tab strip background, status bar, light-dusty popup band |
| border | `#A8BAC8` | pane outlines, sash, gripper |
| chrome | `#B8C9D4` | MDI workspace, dock-between-panes, panel frames, dialog backgrounds, subsystem chrome |
| caption-active-top | `#5A7B95` | active caption gradient top — saturated dusty blue |
| caption-active-bottom | `#3F5C77` | active caption gradient bottom; also inactive caption text, deep accents |
| accent | `#D97757` | editor selection, primary action buttons (the "ottoman" — the one warm point) |
| accent-deep | `#B85A38` | reserved for hover / pressed states on accent elements |

## Per-class tints (property inspector)

The object inspector tints property rows by source class so the most common categories are visually distinct at a glance:

| Class | Hex | Notes |
|---|---|---|
| `window` | `#F8E3D5` | light terracotta — the most important / most-edited category |
| `common` | `#F5E8D5` | light cream — the universal category, recedes |
| `sizerItem` | `#E6EEF5` | light powder — cool tier, distinguishes layout-related props |

These tints intentionally use lighter, less saturated versions of the main palette so they remain readable as row backgrounds.

## Files that own the palette

Most palette values are duplicated inline at the point of use. The places to start when changing a tier:

| File | What it owns |
|---|---|
| `src/engine/frontend/win/theme/luna_dockart.cpp` | AUI dock art — `THEME_COLOUR_MAIN / BORDER / BACKGROUND`, caption gradients, border colour. Top-of-file comment block lists the tier table. |
| `src/engine/frontend/win/theme/luna_tabart.cpp` | AUI tab art — `THEME_COLOUR_MAIN` (active-tab interior), border, tab-strip base colour, the 4 px under-tab "shelf" brush, tab text colours |
| `src/engine/frontend/mainFrame/settings/fontcolorsettings.cpp` | Code editor — per-style foreground / background, terracotta selection |
| `src/engine/frontend/mainFrame/mainFrame.cpp` | MDI client window background (powder blue) |
| `src/engine/frontend/mainFrame/mainFrame.h` | Bottom status bar — chrome-light + caption-active-bottom text |
| `src/engine/frontend/mainFrame/objinspect/objinspect.{h,cpp}` | Property inspector — per-class tints and propgrid caption / margin colours |
| `src/engine/frontend/visualView/ctrl/frame.h` | `wxDefaultStypeFGColour / BGColour` macros — global defaults used for new form controls, toolbars, dataviews, dialogs |
| `src/engine/frontend/win/dlgs/{authorization,activeUser,userList,auditLog}.cpp` | Standalone dialogs — powder-blue background |
| `src/engine/designer/mainFrame/metaTree/tree{Configuration,DataProcessor,DataReport}.cpp` | Metadata-tree panel backgrounds (powder) + tree-ctrl backgrounds (cream) |
| `src/engine/designer/mainFrame/{watch,stack,local}/*.cpp` | Debugger lists — light-cream content tier |
| `src/engine/designer/win/editor/visualEditor/visualEditor.cpp` | Form-design workspace — palest-powder so the form card pops |
| `src/engine/designer/win/editor/visualEditor/titleFrame.cpp` | Form-design title strip — caption-active-bottom + white text |
| `src/engine/designer/win/ctrl/menuBar.cpp` | Designer custom menu bar — chrome powder-blue |
| `src/engine/designer/mainFrame/mainFrameDesignerParts.cpp` and `src/engine/designer/mainFrameDesignerCmd.cpp` | Override of the AUI dock-art background colour — must use chrome `#B8C9D4`, not `wxAUI_DEFAULT_COLOUR` (legacy navy) |
| `src/engine/enterprise/mainFrame/mainFrameEnterprise{Parts,Interface}.cpp` | Enterprise.exe subsystem home page — `THEME_COLOUR_MAIN / BORDER` macros, MDI background, popup interior |
| `src/engine/launcher/{launcher,connectionDB}.cpp` | Launcher main frame + connection dialog — powder chrome, terracotta "Save" primary action |

## What changed from the previous palette

The OES UI went through several palette iterations before landing here. Each one fixed something the previous one broke:

1. **Hard dark navy `#293955` chrome + white captions.** Inherited from the original Luna theme. Looked like an early-2000s MDI app and clashed with modern Windows themes.
2. **Flat slate everywhere.** Lifted to a Tailwind slate scale. Cool and modern but uniformly cold — felt sterile, "too white".
3. **Five-tier cool slate gradation.** Same family but with explicit tier roles. Still too monotone for prolonged use.
4. **1C-style warm cream + steel-blue captions.** Familiar, comfortable, but instantly recognisable as a 1C clone — not distinctive enough.
5. **Cool chrome + cream content + terracotta accent (current).** Inspired by the cool-walls / warm-ottoman interior composition. Calm framing, comfortable content, single warm focal point — distinctive without trying.

When extending the UI, prefer reusing a tier from the table above. If you find yourself needing a new shade, add it to the tier table here in the same edit so the palette stays auditable.
