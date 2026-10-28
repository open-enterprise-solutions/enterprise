/**
 * A shared, light-DOM live region for announcing dynamic updates to screen readers.
 *
 * Live regions rendered inside a shadow root are not reliably announced — notably, JAWS + Firefox ignores them
 * entirely. To work around this, we maintain a single visually-hidden live region appended to `document.body` (light
 * DOM) and update its contents imperatively. This mirrors the approach used by Adobe's React Aria `LiveAnnouncer` and is
 * the most robust way to ensure announcements are picked up across screen readers.
 *
 * The region is created lazily and persists for the lifetime of the page. Each announcement appends a fresh node (so
 * repeating the same message still announces) and removes it shortly after, preventing stale text from accumulating or
 * being re-read when focus returns.
 */
type Politeness = 'polite' | 'assertive';
/**
 * Announces a message to assistive technology via a shared light-DOM live region.
 *
 * @param message - The text to announce. Empty strings are ignored.
 * @param politeness - Whether to announce politely (default) or assertively.
 */
export declare function announce(message: string, politeness?: Politeness): void;
export {};
