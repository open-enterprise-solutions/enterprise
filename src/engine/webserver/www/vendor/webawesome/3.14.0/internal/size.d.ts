/** Canonical size values. */
export type Size = 'xs' | 's' | 'm' | 'l' | 'xl';
/** Deprecated long-form size aliases. */
export type DeprecatedSize = 'small' | 'medium' | 'large';
/** All accepted size values (canonical + deprecated). */
export type SizeWithDeprecated = Size | DeprecatedSize;
/** Normalizes a size value, mapping deprecated long forms to short forms. Returns the value unchanged if already canonical. */
export declare function normalizeSize(size: SizeWithDeprecated): Size;
/** Emits a one-time console warning for deprecated size values. */
export declare function warnDeprecatedSize(tagName: string, value: string): void;
