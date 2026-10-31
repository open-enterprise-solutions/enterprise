import { type PropertyValues } from 'lit';
import { WebAwesomeFormAssociatedElement } from '../../internal/webawesome-form-associated-element.js';
import { LocalizeController } from '../../utilities/localize.js';
import { type DateParts } from './internal/parts.js';
export type WaKnownDateSize = 'xs' | 's' | 'm' | 'l' | 'xl';
export type WaKnownDateAppearance = 'filled' | 'outlined' | 'filled-outlined';
/**
 * @summary Known dates let users enter dates they already know - birthdays, expirations, document
 *  dates - through three separate day, month, and year fields shown in the locale's natural order.
 * @documentation https://webawesome.com/docs/components/known-date
 * @status stable
 * @since 3.8
 *
 * @slot label - The known date's group label. Alternatively, use the `label` attribute.
 * @slot hint - Text that describes how to use the known date. Alternatively, use the `hint` attribute.
 *
 * @event blur - Emitted when the control loses focus.
 * @event change - Emitted when the committed value transitions to a new ISO date.
 * @event focus - Emitted when the control gains focus.
 * @event input - Emitted as the user types in any field.
 * @event wa-invalid - Emitted when the form control has been checked for validity and its constraints aren't satisfied.
 *
 * @csspart form-control - The form control's outer wrapper.
 * @csspart form-control-label - The wrapper inside the legend that styles the visible label text.
 * @csspart form-control-input - Alias on the fields row matching other form controls.
 * @csspart hint - The hint's wrapper.
 * @csspart label - Deprecated. Use the `form-control-label` part instead.
 * @csspart base - Deprecated. Use the `known-date` part instead.
 * @csspart known-date - The component's outer wrapper.
 * @csspart fieldset - The `<fieldset>` element grouping the three fields (or a `role="group"` div).
 * @csspart legend - The `<legend>` element (when a label is present).
 * @csspart fields - The flex row holding the three field blocks.
 * @csspart field - Each field block (label + input).
 * @csspart field-day - Added to the day field block.
 * @csspart field-month - Added to the month field block.
 * @csspart field-year - Added to the year field block.
 * @csspart field-label - The text label above each field's input.
 * @csspart field-input - The native `<input>` inside a field.
 *
 * @cssstate blank - The known date has no committed value.
 * @cssstate disabled - The known date is disabled.
 */
export default class WaKnownDate extends WebAwesomeFormAssociatedElement {
    static css: import("lit").CSSResult[];
    static shadowRootOptions: {
        delegatesFocus: boolean;
        clonable?: boolean;
        customElementRegistry?: CustomElementRegistry;
        mode: ShadowRootMode;
        serializable?: boolean;
        slotAssignment?: SlotAssignmentMode;
    };
    static get validators(): import("../../internal/webawesome-form-associated-element.js").Validator<WebAwesomeFormAssociatedElement>[];
    assumeInteractionOn: string[];
    readonly localize: LocalizeController;
    private readonly hasSlotController;
    private readonly groupId;
    private readonly hintId;
    /** Hidden mirror used for native constraint validation (min/max/required + valid-date roundtrip). */
    valueInput: HTMLInputElement;
    /** Debounces duplicate `change` events when the value hasn't transitioned. */
    private lastEmittedValue;
    private pendingValue;
    /** The three field strings. Stored verbatim so user-typed digits round-trip faithfully. */
    parts: DateParts;
    /** The name submitted with form data. */
    name: string;
    private _value;
    /**
     * The committed value as an ISO `YYYY-MM-DD` string. The setter also accepts a `Date` or `null`. Reading
     * returns an empty string when the value is blank or any field is only partially filled.
     */
    get value(): string;
    set value(val: string | Date | null);
    /** The default value used for form reset. */
    defaultValue: string;
    /** Disables the known date. */
    disabled: boolean;
    /** Makes the known date required for form submission. */
    required: boolean;
    /** Makes the fields non-editable. */
    readonly: boolean;
    /** The known date's size. */
    size: WaKnownDateSize | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /** The known date's visual appearance. */
    appearance: WaKnownDateAppearance;
    /** Draws pill-style fields with rounded edges. */
    pill: boolean;
    /** The known date's label. If you need to display HTML, use the `label` slot instead. */
    label: string;
    /** The known date's hint. If you need to display HTML, use the `hint` slot instead. */
    hint: string;
    /**
     * Browser autofill family. When set to `bday`, the three fields receive `bday-day`, `bday-month`, and
     * `bday-year` respectively. The field-agnostic directives `off` and `on` are applied to all three fields.
     * Any other value is forwarded only to the year field.
     */
    autocomplete: string;
    /** Earliest selectable date as `YYYY-MM-DD`. */
    min: string;
    /** Latest selectable date as `YYYY-MM-DD`. */
    max: string;
    /** BCP-47 locale override. When empty, the inherited `lang` attribute is used. */
    locale: string;
    /** Only required for SSR. Set to `true` if you're slotting in a `label` element. */
    withLabel: boolean;
    /** Only required for SSR. Set to `true` if you're slotting in a `hint` element. */
    withHint: boolean;
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    protected updated(changed: Map<string, unknown>): void;
    /** Focuses the first empty field, or the first field when all are filled. */
    focus(options?: FocusOptions): void;
    /** Removes focus from the known date. */
    blur(): void;
    /** The committed value as a `Date`, or `null` when the value is empty/invalid. */
    get valueAsDate(): Date | null;
    /**
     * Anchor native validation popups on a real visible input. The hidden mirror handles form data, but
     * anchoring a popup on `display: none` content would render it at offset (0, 0).
     */
    get validationTarget(): HTMLElement | undefined;
    formResetCallback(): void;
    formStateRestoreCallback(state: string | File | FormData | null): void;
    private get resolvedLocale();
    private fieldOrder;
    private normalizeIncomingValue;
    private syncPartsFromCanonical;
    private updateHiddenInput;
    private recomputeValue;
    private firstFocusableInput;
    /**
     * Returns the field to fix when the value doesn't compose, in locale order: the first empty field, else the first
     * out-of-range field (year < 1, month not 1–12, day not 1–31), else the day (e.g. Feb 30). Null when the value
     * composes cleanly.
     */
    private firstInvalidField;
    private autocompleteFor;
    private handleFieldInput;
    render(): import("lit-html").TemplateResult<1>;
    private renderField;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-known-date': WaKnownDate;
    }
}
