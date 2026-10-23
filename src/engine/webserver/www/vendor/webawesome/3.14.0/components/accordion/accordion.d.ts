import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../accordion-item/accordion-item.js';
/**
 * @summary Accordions are a vertically stacked set of interactive headings that each contain a title, representing a section of content.
 * @documentation https://webawesome.com/docs/components/accordion
 * @status stable
 * @since 3.8
 *
 * @dependency wa-accordion-item
 *
 * @slot - One or more `<wa-accordion-item>` elements.
 *
 * @event {{ item: WaAccordionItem }} wa-expand - Emitted before an item expands. Cancelable.
 * @event {{ item: WaAccordionItem }} wa-after-expand - Emitted after an item finishes expanding.
 * @event {{ item: WaAccordionItem }} wa-collapse - Emitted before an item collapses. Cancelable.
 * @event {{ item: WaAccordionItem }} wa-after-collapse - Emitted after an item finishes collapsing.
 */
export default class WaAccordion extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private defaultSlot;
    /**
     * Controls how items can be expanded. `multiple` (the default) allows any number of items to be open at
     * once. `single` allows only one item to be open at a time; opening a new item collapses the previously
     * open one, and clicking an open item does not collapse it. `single-collapsible` is the same as `single`
     * except that clicking the open item collapses it, so zero open items is a valid state.
     */
    mode: 'single' | 'single-collapsible' | 'multiple';
    /** The location of the expand/collapse icon in child items. */
    iconPlacement: 'start' | 'end';
    /** The heading level for child item triggers (1–6), or "none" to omit the heading wrapper. Defaults to 3. */
    headingLevel: string;
    /** The accordion's visual appearance. */
    appearance: 'filled' | 'outlined' | 'filled-outlined' | 'plain';
    private getAllItems;
    private getFocusableItems;
    private ownsItem;
    private handleSlotChange;
    private handleKeyDown;
    syncIconPlacement(): void;
    syncHeadingLevel(): void;
    syncAppearance(): void;
    private handleItemTrigger;
    /** Expands all accordion items. No-op when `mode` is `single` or `single-collapsible`. */
    expandAll(): void;
    /** Collapses all accordion items. */
    collapseAll(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-accordion': WaAccordion;
    }
}
