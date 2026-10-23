import { type PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
import '../tooltip/tooltip.js';
import type WaTooltip from '../tooltip/tooltip.js';
/**
 * @summary Copy buttons copy text to the clipboard when the user activates them. They provide built-in success and
 *  error feedback so users know the copy worked.
 * @documentation https://webawesome.com/docs/components/copy
 * @status stable
 * @since 3.6
 *
 * @dependency wa-icon
 * @dependency wa-tooltip
 *
 * @event wa-copy - Emitted when the data has been copied.
 * @event wa-error - Emitted when the data could not be copied.
 *
 * @slot - The trigger element. By default, a copy icon button is rendered so this is optional. If desired, you can slot
 *  in a custom element such as `<wa-button>` or `<button>`.
 * @slot copy-icon - The icon to show in the default copy state. Works best with `<wa-icon>`.
 * @slot success-icon - The icon to show when the content is copied. Works best with `<wa-icon>`.
 * @slot error-icon - The icon to show when a copy error occurs. Works best with `<wa-icon>`.
 *
 * @cssstate success - Applied when the copy operation succeeds.
 * @cssstate error - Applied when the copy operation fails.
 *
 * @csspart button - The internal `<button>` element.
 * @csspart copy-icon - The container that holds the copy icon.
 * @csspart success-icon - The container that holds the success icon.
 * @csspart error-icon - The container that holds the error icon.
 * @csspart feedback - The internal `<wa-tooltip>` element.
 */
export default class WaCopyButton extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly localize;
    copyIcon: HTMLSlotElement;
    successIcon: HTMLSlotElement;
    errorIcon: HTMLSlotElement;
    defaultSlot: HTMLSlotElement;
    shadowTooltip: WaTooltip;
    isCopying: boolean;
    status: 'rest' | 'success' | 'error';
    hasCustomTrigger: boolean;
    private customTriggerEl;
    private lightTooltip;
    private feedbackTimeout;
    private get activeTooltip();
    private get currentLabel();
    /** The text value to copy. */
    value: string;
    /**
     * An id that references an element in the same document from which data will be copied. If both this and `value` are
     * present, this value will take precedence. By default, the target element's `textContent` will be copied. To copy an
     * attribute, append the attribute name wrapped in square brackets, e.g. `from="el[value]"`. To copy a property,
     * append a dot and the property name, e.g. `from="el.value"`.
     */
    from: string;
    /** Disables the copy button. */
    disabled: boolean;
    /** A custom label to use as the accessible name and tooltip text in the default copy state. */
    copyLabel: string;
    /** A custom label to show in the tooltip after copying. */
    successLabel: string;
    /** A custom label to show in the tooltip when a copy error occurs. */
    errorLabel: string;
    /** The length of time to show feedback before restoring the default trigger. */
    feedbackDuration: number;
    /** The preferred placement of the tooltip. */
    tooltipPlacement: 'top' | 'right' | 'bottom' | 'left';
    /**
     * Controls the built-in tooltip. `full` (default) shows the tooltip on hover and focus and during copy feedback.
     * `copy` keeps the tooltip silent on hover/focus and only shows it briefly to confirm a successful or failed copy.
     * `none` disables the tooltip entirely. Applies to both the default and custom triggers.
     */
    tooltip: 'full' | 'copy' | 'none';
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    disconnectedCallback(): void;
    handleStatusChange(): void;
    handleLabelChange(): void;
    handleTooltipOptionsChange(): void;
    handleTooltipModeChange(oldValue?: 'full' | 'copy' | 'none'): void;
    private handleDefaultSlotChange;
    private releaseAssignedId;
    private ensureLightTooltip;
    private removeLightTooltip;
    private syncTooltipText;
    private handleCopy;
    private showStatus;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-copy-button': WaCopyButton;
    }
}
