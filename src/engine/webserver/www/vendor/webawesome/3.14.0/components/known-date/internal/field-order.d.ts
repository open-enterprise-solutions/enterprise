/**
 * Locale-aware field ordering for `<wa-known-date>`'s three fields.
 *
 * The control renders day, month, and year in the order the locale presents them (`MDY` for en-US,
 * `DMY` for en-GB/de-DE, `YMD` for ja-JP/ISO locales). We derive that order by probing
 * `Intl.DateTimeFormat.formatToParts()` for a date whose parts are mutually unambiguous, then reading
 * back which field appears first, second, and third. Only the order is taken from the locale — the
 * fields themselves are always day/month/year.
 */
export type SegmentField = 'year' | 'month' | 'day';
/** The day/month/year order for a locale. Cache key is the full locale string (`en-GB` ≠ `en-US`). */
export declare function localeFieldOrder(locale: string): SegmentField[];
