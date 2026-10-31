import type { Validator } from '../../../internal/webawesome-form-associated-element.js';
/**
 * Reports `badInput` for any entry that has digits but doesn't compose to a canonical date — a partially
 * filled set of fields (some but not all) or a complete-but-impossible combination (e.g. day 32, month 13,
 * Feb 30). Like `<wa-time-input>`, this surfaces through the native constraint validation flow (the browser
 * popup on submit), not as live inline messaging.
 *
 * The empty case is valid here; the empty + required case is handled by `RequiredValidator`, so we inherit
 * the browser-localized native "Please fill out this field" message.
 */
export declare const PartialDateValidator: () => Validator;
