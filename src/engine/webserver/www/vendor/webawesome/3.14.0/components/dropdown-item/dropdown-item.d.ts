import type { PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
/**
 * @summary Dropdown items represent selectable entries within a dropdown menu, including standard actions, checkable
 *  items, and submenu triggers.
 * @documentation https://webawesome.com/docs/components/dropdown-item
 * @status stable
 * @since 3.0
 *
 * @dependency wa-icon
 *
 * @event blur - Emitted when the dropdown item loses focus.
 * @event focus - Emitted when the dropdown item gains focus.
 *
 * @slot - The dropdown item's label.
 * @slot icon - An optional icon to display before the label.
 * @slot details - Additional content or details to display after the label.
 * @slot submenu - Submenu items, typically `<wa-dropdown-item>` elements, to create a nested menu.
 *
 * @csspart checkmark - The checkmark icon (a `<wa-icon>` element) when the item is a checkbox.
 * @csspart icon - The container for the icon slot.
 * @csspart label - The container for the label slot.
 * @csspart details - The container for the details slot.
 * @csspart submenu-icon - The submenu indicator icon (a `<wa-icon>` element).
 * @csspart submenu - The submenu container.
 *
 * @cssstate active - Applied when the item is the active item in the menu.
 * @cssstate checked - Applied when the item is checked.
 * @cssstate disabled - Applied when the item is disabled.
 * @cssstate has-submenu - Applied when the item has a submenu.
 * @cssstate link - Applied when the item is a link (i.e. `href` is set).
 * @cssstate submenu-open - Applied when the item's submenu is open.
 */
export default class WaDropdownItem extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private readonly hasSlotController;
    submenuElement: HTMLDivElement;
    private linkElement;
    /** @internal The controller will set this property to true when the item is active. */
    active: boolean;
    /** The type of menu item to render. */
    variant: 'danger' | 'default';
    /**
     * @internal The dropdown item's size.
     */
    size: 'xs' | 's' | 'm' | 'l' | 'xl' | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /**
     * @internal The controller will set this property to true when at least one checkbox exists in the dropdown. This
     * allows non-checkbox items to draw additional space to align properly with checkbox items.
     */
    checkboxAdjacent: boolean;
    /**
     * @internal The controller will set this property to true when at least one item with a submenu exists in the
     * dropdown. This allows non-submenu items to draw additional space to align properly with items that have submenus.
     */
    submenuAdjacent: boolean;
    /**
     * An optional value for the menu item. This is useful for determining which item was selected when listening to the
     * dropdown's `wa-select` event.
     */
    value: string;
    /** Set to `checkbox` to make the item a checkbox. */
    type: 'normal' | 'checkbox';
    /** Set to true to check the dropdown item. Only valid when `type` is `checkbox`. */
    checked: boolean;
    /** Disables the dropdown item. */
    disabled: boolean;
    /** Whether the submenu is currently open. */
    submenuOpen: boolean;
    /**
     * When set, selecting the item will navigate to this URL. The item remains a menu item for assistive devices, so
     * make sure the label describes where the link goes. Ignored when the item has a submenu.
     */
    href: string;
    /** Tells the browser where to open the link. Only used when `href` is present. */
    target: '_blank' | '_parent' | '_self' | '_top';
    /** When using `href`, this attribute will map to the underlying link's `rel` attribute. */
    rel: string;
    /** Tells the browser to download the linked file as this filename. Only used when `href` is present. */
    download: string;
    /** @internal Store whether this item has a submenu */
    hasSubmenu: boolean;
    connectedCallback(): void;
    disconnectedCallback(): void;
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    updated(changedProperties: PropertyValues<this>): void;
    private handleSlotChange;
    /** Update the has-submenu custom state */
    private updateHasSubmenuState;
    /** Opens the submenu. */
    openSubmenu(): Promise<void>;
    /** Notifies the parent dropdown that this item is opening its submenu */
    private notifyParentOfOpening;
    /** Closes the submenu. */
    closeSubmenu(): Promise<void>;
    /** Determines whether the item navigates when selected. Items with submenus never navigate. */
    private isLink;
    /**
     * @internal Navigates to the item's `href` by clicking the hidden link in the shadow root. The event that triggered
     * the selection is passed along so modifier keys, such as pressing Command or Control to open a new tab, are honored.
     * Note that Safari ignores modifier keys on synthetic clicks.
     */
    navigate(sourceEvent?: MouseEvent | KeyboardEvent): void;
    /** Gets all dropdown items in the submenu. */
    private getSubmenuItems;
    /** Prevents click events from firing on the host when the item is disabled (e.g. programmatic .click() calls). */
    private handleHostClick;
    /** Prevents click events from firing when the item is disabled. */
    private handleClick;
    /** Handles pointer enter to open the submenu on hover */
    private handlePointerEnter;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-dropdown-item': WaDropdownItem;
    }
}
