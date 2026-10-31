/**
 * Pure, DOM-free model for the **segmented** time input of `<wa-time-input>`. Plays the same role for time-input as
 * `date-input/internal/segments.ts` does for date-input — only this one is wired to the generic
 * `SegmentedFieldController` via the `timeSegmentRules()` factory below.
 *
 * A `<wa-time-input>` renders hour, minute, optional seconds, and optional AM/PM as discrete spinbutton segments in
 * the user's locale order, separated by inert literal text (`:`, NBSPs, locale-specific glyphs). Each segment is edited
 * independently: digits fill the focused segment with auto-advance, Arrow Up/Down step within bounds, and AM/PM toggles
 * via `a`/`p` keys or arrow stepping.
 *
 * Wire format mirrors HTML `<input type="time">`: `HH:mm` for whole-minute steps, `HH:mm:ss` for sub-minute steps,
 * `HH:mm:ss.sss` for fractional-second steps. The wire value is always 24-hour even when the UI is 12-hour.
 */
import type { SegmentLayout, SegmentRules, TypeDigitResult } from '../../../internal/segmented-field/segmented-field-controller.js';
export type TimeField = 'hour' | 'minute' | 'second' | 'dayPeriod';
/** Partially- or fully-entered time. `null` means the segment is empty. */
export interface TimeSegments {
    /** Hour in display units: 0–23 when `hour12` is false, 1–12 when true. */
    hour: number | null;
    minute: number | null;
    /** Whole seconds. `null` when the seconds segment isn't shown or hasn't been entered. */
    second: number | null;
    /** 0 = AM, 1 = PM. `null` when the segment isn't shown or hasn't been entered. */
    dayPeriod: 0 | 1 | null;
}
export interface BuildLayoutOptions {
    hour12: boolean;
    withSeconds: boolean;
}
/** Clear the layout cache (intended for tests). */
export declare function clearTimeSegmentLayoutCache(): void;
/**
 * Build (and cache) the segment layout for a locale by probing `Intl.DateTimeFormat.formatToParts`. Literal parts
 * (colons, NBSPs) are preserved verbatim so locale-specific separators render correctly. Digits are forced to Latin
 * via `numberingSystem: 'latn'` — the wire value is always ASCII; only segment order and AM/PM glyphs vary by locale.
 *
 * Cache key: `${locale}|${hour12}|${withSeconds}`.
 */
export declare function buildTimeSegmentLayout(locale: string, opts: BuildLayoutOptions): SegmentLayout;
/** Resolve whether a locale defaults to 12-hour clock. Used when `hourFormat='auto'`. */
export declare function resolveHour12(locale: string): boolean;
/** Format the localized AM/PM string for a given `dayPeriod` value (0=AM, 1=PM). Falls back to "AM"/"PM". */
export declare function formatDayPeriod(locale: string, period: 0 | 1): string;
export declare function timeSegmentBounds(field: TimeField, hour12: boolean): {
    min: number;
    max: number;
};
/**
 * Step a segment by `delta` (+1 / -1). Hour, minute, second **wrap** within their bounds with **no carry** into other
 * fields (mirrors native `<input type="time">`). DayPeriod toggles. In 12-hour mode, stepping the hour does not toggle
 * AM/PM — matches Chrome and React Aria. Empty segments seed from `now` (or 12/0 for hour, 0 for minute/second, AM for
 * dayPeriod).
 */
export declare function stepTimeSegment(segments: TimeSegments, field: TimeField, delta: -1 | 1, hour12: boolean, now?: Date): TimeSegments;
/**
 * Apply a digit to a time segment. Per-field rules:
 *
 * - **Hour (24-hour, 0–23):** empty + `3..9` commits + advances; empty + `0..2` buffers; buffer `0`/`1` + any digit
 *   combines to `0d`/`1d` (advance); buffer `2` + `0..3` combines to `20..23` (advance); buffer `2` + `4..9` replaces.
 *   `00` stays buffered at `0`.
 * - **Hour (12-hour, 1–12):** same algorithm as the date-input's month (1–12).
 * - **Minute / Second (0–59):** empty + `6..9` commits + advances; empty + `0..5` buffers; buffer + any digit combines
 *   to `0d..59` (advance); buffer + overflow digit replaces.
 *
 * Returns the same `TypeDigitResult` shape the controller expects.
 */
export declare function typeTimeDigit(field: TimeField, buffer: string, digit: string, hour12: boolean): TypeDigitResult;
/** Interpret a raw digit buffer as its numeric value. */
export declare function bufferToValue(buffer: string): number | null;
/** Set AM/PM directly via keystroke (`a`/`A`/`p`/`P`). Returns the new `dayPeriod` value or `null` for no-op. */
export declare function dayPeriodFromKey(key: string): 0 | 1 | null;
export declare function formatTimeSegmentText(field: TimeField, value: number | null, buffer: string, placeholder: string, locale: string): string;
export interface WireOptions {
    hour12: boolean;
    withSeconds: boolean;
}
/** True if every required segment is filled. */
export declare function isTimeComplete(segments: TimeSegments, opts: WireOptions): boolean;
/** True if no segment is filled. */
export declare function isTimeEmpty(segments: TimeSegments): boolean;
/**
 * Convert a display-segment group to the canonical HTML `<input type="time">` wire format:
 *  - `HH:mm` when `withSeconds` is false.
 *  - `HH:mm:ss` when `withSeconds` is true.
 *
 * Always 24-hour zero-padded. Returns `''` for incomplete groups.
 */
export declare function timeSegmentsToWire(segments: TimeSegments, opts: WireOptions): string;
/**
 * Parse the canonical HTML wire format into display segments. Accepts the spec's `HH:mm`, `HH:mm:ss`, and
 * `HH:mm:ss.sss` forms. Returns all-null segments for an empty/invalid string. The fractional component (if any) is
 * preserved on the seconds segment as a whole second (rounded down).
 */
export declare function wireToTimeSegments(wire: string | null | undefined, opts: WireOptions): TimeSegments;
/**
 * Resolve whether the seconds segment should be shown given a `step` value (in seconds, per HTML spec). Mirrors native
 * `<input type="time">`: `step >= 60` and divisible by 60 → no seconds; otherwise → seconds shown. `'any'` → seconds
 * shown (matches modern Chromium).
 */
export declare function withSecondsForStep(step: number | 'any'): boolean;
/**
 * Build the `SegmentRules` object the generic `SegmentedFieldController` expects, wired to a host's segment-store
 * getter/setter. Lets the host say "rules: timeSegmentRules({ getSegments, setSegments, hour12, now })" and the
 * controller handles all the rest.
 */
export declare function timeSegmentRules(opts: {
    getSegments: (group: string) => TimeSegments;
    setSegments: (group: string, next: TimeSegments) => void;
    hour12: () => boolean;
    now?: () => Date;
}): SegmentRules;
