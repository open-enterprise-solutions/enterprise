/**
 * Pure, DOM-free helpers for `<wa-known-date>`'s three-field model.
 *
 * Each field stores the user's raw digit string (so `'07'` and `'7'` are preserved verbatim while typing).
 * Only `partsToIso` normalizes the trio into a canonical `YYYY-MM-DD` string, and only when every field
 * is filled AND the combination is a real calendar date.
 */
export interface DateParts {
    day: string;
    month: string;
    year: string;
}
export declare const EMPTY_PARTS: DateParts;
/** True when every field is filled. (Does not imply the combination is a valid calendar date.) */
export declare function isComplete(parts: DateParts): boolean;
/** True when no field is filled. */
export declare function isEmpty(parts: DateParts): boolean;
/**
 * Compose three field strings into a canonical ISO `YYYY-MM-DD`. Returns `''` when:
 *  - any field is empty,
 *  - any field isn't a positive integer in its bounds (year 1–9999, month 1–12, day 1–31), or
 *  - the combination is not a real calendar date (Feb 30, Feb 29 in a non-leap year, …).
 */
export declare function partsToIso(parts: DateParts): string;
/** Split an ISO `YYYY-MM-DD` into three zero-padded field strings. Returns `EMPTY_PARTS` for an empty string. */
export declare function isoToParts(iso: string): DateParts;
