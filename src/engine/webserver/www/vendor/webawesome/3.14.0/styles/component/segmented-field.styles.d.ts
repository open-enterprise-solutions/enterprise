/**
 * Shared trailing-affordance styling for segmented-field pickers (`<wa-date-input>`,
 * `<wa-time-input>`). Keeps their clear and expand buttons — and their start/end decoration
 * slots — on the same trailing optical axis as `<wa-select>`'s chevron and end-slot icons,
 * while preserving each button's padded hit target and focus ring.
 *
 * How the alignment works:
 *  - Each button keeps `0.25em` padding (hit area + focus-ring breathing room).
 *  - `margin-inline-end: -0.25em` lets that trailing padding overhang the trailing content
 *    edge instead of pushing the glyph inboard.
 *  - Each button is given a fixed `inline-size` equal to its natural width (glyph + 2×padding):
 *    `1.75em` for the expand glyph (1.25em) and `1.5em` for the clear glyph (1em). Because the
 *    width is fixed and the glyph is centered, a custom-slotted icon of any width stays centered
 *    on the axis — the alignment is glyph-agnostic, not tuned to one icon.
 *  - The expand button's `margin-inline-start` sets the gap that positions the clear button onto
 *    `<wa-select>`'s clear axis given those fixed widths.
 */
declare const _default: import("lit").CSSResult;
export default _default;
