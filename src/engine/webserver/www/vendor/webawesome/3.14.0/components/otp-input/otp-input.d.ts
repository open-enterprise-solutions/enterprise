import { type PropertyValues } from 'lit';
import { WebAwesomeFormAssociatedElement } from '../../internal/webawesome-form-associated-element.js';
/**
 * @summary OTP inputs collect one-time passcodes, PINs, and other fixed-length codes, one character per segment.
 * Use them for SMS verification, two-factor authentication, and invite codes.
 * @documentation https://webawesome.com/docs/components/otp-input
 * @status stable
 * @since 3.11
 *
 * @slot label - An optional label. Use this for labels that contain HTML. When `label` attribute is set it takes priority.
 * @slot hint - Optional hint text. Use this for hints that contain HTML. When `hint` attribute is set it takes priority.
 *
 * @event focus - Emitted when the control gains focus.
 * @event blur - Emitted when the control loses focus.
 * @event input - Emitted when a character is entered or removed.
 * @event change - Emitted when the value changes and the field loses focus.
 * @event wa-complete - Emitted once when all segments are filled. Cancelable — call `preventDefault()` to stop
 *   `autosubmit` from submitting the form for this completion.
 * @event wa-clear - Emitted when the control's value is cleared.
 * @event wa-invalid - Emitted when the form control has been checked for validity and its constraints aren't satisfied.
 *
 * @csspart label - The label element.
 * @csspart hint - The hint element.
 * @csspart segments - The wrapper around all segment cells and separators.
 * @csspart segment - An individual character segment cell.
 * @csspart segment-literal - Inert literal text between segment groups (e.g. space or dash).
 *
 * @cssstate --blank - Applied when no characters have been entered.
 * @cssstate --filled - Applied when all segments are filled.
 * @cssstate disabled - Applied when the component is disabled.
 * @cssstate readonly - Applied when the component is readonly.
 * @cssstate user-invalid - Applied when validation fails after interaction.
 *
 * @cssproperty [--segment-size=2.5em] - Width and height of each segment cell.
 * @cssproperty [--segment-gap=var(--wa-space-xs)] - Gap between segments (not used in `contained` appearance).
 * @cssproperty [--segment-border-radius=var(--wa-form-control-border-radius)] - Corner radius of each segment.
 * @cssproperty [--mask-char='•'] - Character shown in place of entered values when `mask` is set, and as a hint
 *   in empty segments when `with-mask` is set.
 */
export default class WaOtpInput extends WebAwesomeFormAssociatedElement {
    static shadowRootOptions: {
        delegatesFocus: boolean;
        clonable?: boolean;
        customElementRegistry?: CustomElementRegistry;
        mode: ShadowRootMode;
        serializable?: boolean;
        slotAssignment?: SlotAssignmentMode;
    };
    static css: import("lit").CSSResult[];
    static get validators(): import("../../internal/webawesome-form-associated-element.js").Validator<WebAwesomeFormAssociatedElement>[];
    private readonly hasSlotController;
    /** The real `<input>` used for form association and validation (visually hidden). */
    input: HTMLInputElement;
    private segmentsContainer;
    get validationTarget(): HTMLElement;
    private _focused;
    private _activeIndex;
    private _selectionAnchor;
    private _pendingClickIndex;
    private get hasSelection();
    private setCaretIndex;
    private _value;
    /** The current value of the OTP field, submitted as a name/value pair with form data. */
    get value(): string;
    set value(val: string);
    /** The default value. Used to restore the field on form reset. Reflects the `value` HTML attribute. */
    defaultValue: string | null;
    /** Number of character segments to display. Overridden by `format` when set. */
    length: number;
    /** Visual appearance of the segments. */
    appearance: 'outlined' | 'filled' | 'filled-outlined' | 'contained';
    /** Allowed character class. */
    type: 'numeric' | 'alpha' | 'alphanumeric';
    /** When true, entered characters are displayed as `--mask-char` instead of their real value. */
    mask: boolean;
    /** Case transformation applied to entered characters. */
    case: 'preserve' | 'upper' | 'lower';
    /** The size of each segment. */
    size: 'xs' | 's' | 'm' | 'l' | 'xl' | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /** A label shown above the segments. Use the `label` slot for HTML content. */
    label: string;
    /** Hint text shown below the segments. Use the `hint` slot for HTML content. */
    hint: string;
    /**
     * Segment format string using `#` as a segment placeholder and any other character as a literal separator.
     * Setting `format` overrides `length` (the segment count is derived from the number of `#` characters).
     * @example "### ###" → two groups of three with a space between them
     * @example "####-####" → two groups of four joined by a dash
     */
    format: string;
    /** The `autocomplete` attribute forwarded to the underlying input. */
    autocomplete: string;
    /** Makes the field required. A partially-filled field is always invalid regardless of this attribute. */
    required: boolean;
    /** Makes the field readonly — the value displays but cannot be edited by the user. */
    readonly: boolean;
    /** When true, the form is submitted automatically once all segments are filled. */
    autosubmit: boolean;
    /** Automatically focuses the field when the page loads. */
    autofocus: boolean;
    /**
     * When true, empty segments show `--mask-char` as a hint instead of appearing blank, similar to
     * how a password field communicates its expected length before anything is typed.
     */
    withMask: boolean;
    assumeInteractionOn: string[];
    private _lastChangeValue;
    /** Number of segments derived from `format` (count of `#`) or `length`. */
    get effectiveLength(): number;
    /** Parsed format array — each entry is a segment slot or a literal separator character. */
    private get parsedFormat();
    private filterAndTransform;
    protected willUpdate(changedProperties: PropertyValues): void;
    protected updated(changedProperties: PropertyValues): void;
    private syncCursor;
    formResetCallback(): void;
    private handleInput;
    private maybeDispatchComplete;
    private handleKeyDown;
    private spliceValue;
    private handlePaste;
    private handleFocus;
    private handleSelect;
    private handleBlur;
    private segmentIndexAt;
    private handleSegmentsPointerDown;
    private handleSegmentsClick;
    /** Clears the current value and returns focus to the field. */
    clear(): void;
    /** Focuses the field. */
    focus(options?: FocusOptions): void;
    /** Removes focus from the field. */
    blur(): void;
    /** Selects all entered characters in the hidden input. */
    select(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-otp-input': WaOtpInput;
    }
}
