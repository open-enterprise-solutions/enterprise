import { type PropertyValues } from 'lit';
import type { TemplateResult } from 'lit-html';
import '../../components/popup/popup.js';
import type WaPopup from '../../components/popup/popup.js';
import { WebAwesomeFormAssociatedElement } from '../../internal/webawesome-form-associated-element.js';
export type WaTimeInputSize = 'xs' | 's' | 'm' | 'l' | 'xl';
export type WaTimeInputPlacement = 'top' | 'top-start' | 'top-end' | 'bottom' | 'bottom-start' | 'bottom-end';
export type WaTimeInputHourFormat = 'auto' | '12' | '24';
/**
 * @summary Time pickers let users enter a time through a segmented field or select one visually from a popup column
 *  picker. They support 12- and 24-hour formats, optional seconds, and locale-aware segment order.
 * @documentation https://webawesome.com/docs/components/time-input
 * @status stable
 * @since 3.8
 *
 * @dependency wa-icon
 * @dependency wa-popup
 *
 * @slot label - The time picker's label. Alternatively, use the `label` attribute.
 * @slot hint - Text that describes how to use the time picker. Alternatively, use the `hint` attribute.
 * @slot start - An element placed at the start of the input.
 * @slot end - An element placed at the end of the input.
 * @slot clear-icon - An icon to use in lieu of the default clear icon.
 * @slot expand-icon - The icon to show on the popup toggle button. Defaults to a clock icon.
 * @slot footer - Content shown below the column picker in the popup. Replaces the default Now button when present.
 *
 * @event change - Emitted when the committed value changes.
 * @event input - Emitted as the user types into a segment or interacts with the popup columns.
 * @event focus - Emitted when the control receives focus.
 * @event blur - Emitted when the control loses focus.
 * @event wa-clear - Emitted when the clear button is activated.
 * @event wa-show - Emitted when the popup is about to open. Cancelable.
 * @event wa-after-show - Emitted after the popup opens and animations complete.
 * @event wa-hide - Emitted when the popup is about to close. Cancelable.
 * @event wa-after-hide - Emitted after the popup closes and animations complete.
 * @event wa-invalid - Emitted when the form control has been checked for validity and its constraints aren't satisfied.
 *
 * @csspart form-control - The form control that wraps the label, input, and hint.
 * @csspart form-control-label - The label.
 * @csspart label - Deprecated. Use the `form-control-label` part instead.
 * @csspart form-control-input - The input's wrapper.
 * @csspart hint - The hint's wrapper.
 * @csspart base - Deprecated. Use the `time-input` part instead.
 * @csspart time-input - The component's outer wrapper.
 * @csspart input-wrapper - The container around the start slot, segmented input, clear button, and expand button.
 * @csspart start - The container that wraps the `start` slot.
 * @csspart end - The container that wraps the `end` slot.
 * @csspart input - The segmented input group.
 * @csspart segment - Each editable segment (hour/minute/second/AM-PM spinbutton). Use `[part~="segment"]` to style all.
 * @csspart segment-literal - Inert literal text between segments (separators).
 * @csspart clear-button - The clear button.
 * @csspart expand-button - The popup toggle button.
 * @csspart expand-icon - The expand icon wrapper.
 * @csspart popup - The popup container.
 * @csspart columns - The row of column listboxes inside the popup.
 * @csspart column - Each column listbox.
 * @csspart column-item - Each option inside a column.
 * @csspart column-item-selected - The currently selected option inside a column.
 * @csspart now-button - The default "Now" button rendered in the popup footer when `with-now` is set.
 *
 * @cssproperty [--show-duration=var(--wa-transition-fast)] - The duration of the show animation.
 * @cssproperty [--hide-duration=var(--wa-transition-fast)] - The duration of the hide animation.
 * @cssproperty [--column-item-height=2.25em] - Height of each option inside a popup column.
 * @cssproperty [--column-width=3em] - Width of each popup column.
 *
 * @cssstate blank - The time picker has no committed value.
 * @cssstate open - The popup is open.
 * @cssstate disabled - The time picker is disabled.
 */
export default class WaTimeInput extends WebAwesomeFormAssociatedElement {
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
    /** Every segment edit dispatches `input`, so a single observed `input` event marks the field as interacted with. */
    assumeInteractionOn: string[];
    private readonly hasSlotController;
    private readonly localize;
    private readonly popupId;
    private readonly keyboardHelpId;
    private pendingValue;
    /** When true, the next `show()` will move focus into the first popup column (set by Alt+ArrowDown). */
    private moveFocusToColumnOnShow;
    /** Debounces duplicate `change` events when the value hasn't transitioned. */
    private lastEmittedValue;
    popup: WaPopup;
    valueInput: HTMLInputElement;
    /** The segments displayed in the input and popup. The wire value is derived from these. */
    private segments;
    /**
     * Generic segmented-input plumbing. Owns per-segment digit buffers, roving tabindex, navigation keys
     * (arrows / Home / End / Tab flush / Backspace / Delete), and separator advance. Time-specific rules
     * (per-field digit semantics, wraparound stepping, layout derivation) are plugged in below.
     */
    private readonly segmentsController;
    /** Localized term lookup. Falls back to the English string if a locale hasn't translated the key yet. */
    private term;
    get validationTarget(): HTMLInputElement;
    /** The time picker's name, submitted as a name/value pair with form data. */
    name: string;
    private _value;
    /**
     * The time picker's value as a wire-format string matching HTML `<input type="time">`: `HH:mm`, `HH:mm:ss`, or
     * `HH:mm:ss.sss` (always 24-hour). The setter also accepts a `Date` (extracts local h/m/s) or `null`.
     */
    get value(): string;
    set value(val: string | Date | null);
    /** The default value of the form control. Used for form reset. */
    defaultValue: string;
    /** Disables the time picker. */
    disabled: boolean;
    /** Makes the time picker required for form submission. */
    required: boolean;
    /** Makes the input non-editable. The popup still opens for browsing. */
    readonly: boolean;
    /** The time picker's size. */
    size: WaTimeInputSize | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /** The time picker's visual appearance. */
    appearance: 'filled' | 'outlined' | 'filled-outlined';
    /** Draws a pill-style time picker with rounded edges. */
    pill: boolean;
    /** The time picker's label. If you need to display HTML, use the `label` slot instead. */
    label: string;
    /** The time picker's hint. If you need to display HTML, use the `hint` slot instead. */
    hint: string;
    /** Forwarded to the hidden form input to enable browser autofill (`on`/`off`/custom tokens). */
    autocomplete: string;
    /** Shows a clear button when the time picker has a value. */
    withClear: boolean;
    /** Renders a "Now" button in the popup footer. */
    withNow: boolean;
    /** Only required for SSR. Set to `true` if you're slotting in a `label` element. */
    withLabel: boolean;
    /** Only required for SSR. Set to `true` if you're slotting in a `hint` element. */
    withHint: boolean;
    /**
     * The earliest selectable time in wire format. May be later than `max` to represent an overnight range. The picker
     * delegates reversed-range semantics to the mirrored native `<input type="time">`.
     */
    min: string;
    /** The latest selectable time in wire format. */
    max: string;
    /**
     * The granularity, in seconds, matching HTML `<input type="time">`. Default `60` hides the seconds segment.
     * Values below 60 reveal the seconds segment. `'any'` disables `stepMismatch` enforcement.
     */
    step: number | 'any';
    /** Whether the UI uses a 12-hour or 24-hour clock. `auto` follows the resolved locale. */
    hourFormat: WaTimeInputHourFormat;
    /** Whether the popup is open. */
    open: boolean;
    /** Preferred popup placement. */
    placement: WaTimeInputPlacement;
    /** Distance in pixels between the popup and the input. */
    distance: number;
    disconnectedCallback(): void;
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    protected updated(changed: Map<string, unknown>): void;
    handleDisabledChange(): void;
    handleOpenChange(): Promise<void>;
    /** Sets focus on the first empty (else first) segment. */
    focus(options?: FocusOptions): void;
    /** Removes focus from the time picker. */
    blur(): void;
    /** Opens the popup. */
    show(): Promise<void>;
    /** Closes the popup. */
    hide(): Promise<void>;
    /** The time as a `Date` (today + wire value), or `null` when empty. */
    get valueAsDate(): Date | null;
    /** Milliseconds since midnight, or `NaN` when empty. */
    get valueAsNumber(): number;
    formResetCallback(): void;
    formStateRestoreCallback(state: string | File | FormData | null): void;
    private get resolvedLocale();
    private get isRtl();
    /** Final hour12 decision: `auto` defers to the locale; explicit `'12'` / `'24'` overrides. */
    private get resolvedHour12();
    /** Whether the seconds segment is shown given the current `step` value. */
    private get resolvedWithSeconds();
    private getLayout;
    private normalizeIncomingValue;
    /** Recompute the segment state from the canonical `_value`. Discards any in-progress digit buffers. */
    private syncSegmentsFromCanonical;
    private updateHiddenInput;
    /**
     * Recompute the canonical value from the current segments. Fires `input` always, and `change` when the value
     * transitions, matching `<input type="time">` semantics.
     */
    private recomputeValue;
    private addOpenListeners;
    private removeOpenListeners;
    private handleDocumentFocusIn;
    private handleDocumentKeyDown;
    private handleDocumentMouseDown;
    private focusActiveSegment;
    private handleSegmentFocus;
    private handleSegmentBlur;
    private handleInputWrapperPointerDown;
    private handleSegmentKeyDown;
    private handleExpandButtonClick;
    private handleClearClick;
    private handleClearMouseDown;
    private handleNowClick;
    /** Field list for the visible columns, based on the resolved layout. */
    private get columnFields();
    private columnItemsFor;
    private focusFirstColumn;
    private scrollColumnsToCurrent;
    /**
     * Scroll `column` only as far as needed to keep `item` in view. Equivalent to `scrollIntoView({ block: 'nearest' })`
     * but scoped to the column so it can't scroll ancestors (the page, popup, etc.). Measured with
     * `getBoundingClientRect` because the items' `offsetParent` isn't the column.
     */
    private keepItemInView;
    private handleColumnItemClick;
    private handleColumnKeyDown;
    /** Visual placeholder rendered in an empty segment. Matches the native `<input type="time">` UI. */
    private placeholderFor;
    /** Localized readable name of the field, used for the spinbutton's aria-label. */
    private fieldLabelFor;
    private segmentAriaValueText;
    render(): TemplateResult<1>;
    private renderSegmentGroup;
    private renderSegment;
    private renderColumn;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-time-input': WaTimeInput;
    }
}
