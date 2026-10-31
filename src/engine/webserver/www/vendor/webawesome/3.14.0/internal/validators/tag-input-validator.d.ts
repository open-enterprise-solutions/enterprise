import type WaTagInput from '../../components/tag-input/tag-input.js';
import type { Validator } from '../webawesome-form-associated-element.js';
/**
 * Validates the number of tags in a tag input against `required`, `min-tags`, and `max-tags`. The built-in required
 * validator only checks whether the value is falsy, which an empty array never is, so tag inputs need their own.
 */
export declare const TagInputValidator: () => Validator<WaTagInput>;
