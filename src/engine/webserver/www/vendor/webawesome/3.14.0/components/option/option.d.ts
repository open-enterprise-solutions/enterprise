import type { PropertyValues } from 'lit';
import { nothing } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
/**
 * @summary Options represent the individual choices inside a select or similar form control. Each option holds a value
 *  and the label shown to the user.
 * @documentation https://webawesome.com/docs/components/option
 * @status stable
 * @since 2.0
 *
 * @dependency wa-icon
 *
 * @slot - The option's label.
 * @slot start - An element, such as `<wa-icon>`, placed before the label.
 * @slot end - An element, such as `<wa-icon>`, placed after the label.
 *
 * @csspart checked-icon - The checked icon, a `<wa-icon>` element.
 * @csspart label - The option's label.
 * @csspart start - The container that wraps the `start` slot.
 * @csspart end - The container that wraps the `end` slot.
 *
 * @cssstate current - The user has keyed into the option, but hasn't selected it yet (shows a highlight)
 * @cssstate selected - The option is selected and has aria-selected="true"
 * @cssstate disabled - Applied when the option is disabled
 * @cssstate hover - Like `:hover` but works while dragging in Safari
 *
 * @cssproperty --current-text-color - The text color of the current (highlighted) option, paired with `--wa-form-control-activated-color`.
 */
export default class WaOption extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private readonly localize;
    private cachedDefaultLabel;
    private isInitialized;
    private isDefaultLabelDirty;
    defaultSlot: HTMLSlotElement;
    current: boolean;
    /**
     * The option's value. When selected, the containing form control will receive this value. The value must be unique
     * from other options in the same group. Values may not contain spaces, as spaces are used as delimiters when listing
     * multiple values.
     */
    value: string;
    /** Draws the option in a disabled state, preventing selection. */
    disabled: boolean;
    /** @internal */
    selected: boolean;
    /** Selects an option initially. */
    defaultSelected: boolean;
    _label: string;
    /**
     * The option’s plain text label.
     * Usually automatically generated, but can be useful to provide manually for cases involving complex content.
     */
    set label(value: string);
    get label(): string;
    /** The default label, generated from the element contents. Will be equal to `label` in most cases. */
    get defaultLabel(): string;
    connectedCallback(): void;
    disconnectedCallback(): void;
    private handleDefaultSlotChange;
    private handleHover;
    protected willUpdate(changedProperties: PropertyValues<this>): void;
    syncDefaultSelected(): void;
    updated(changedProperties: PropertyValues<this>): void;
    protected firstUpdated(changedProperties: PropertyValues<this>): Promise<void>;
    private updateDefaultLabel;
    render(): import("lit-html").TemplateResult<1> | typeof nothing;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-option': WaOption;
    }
}
