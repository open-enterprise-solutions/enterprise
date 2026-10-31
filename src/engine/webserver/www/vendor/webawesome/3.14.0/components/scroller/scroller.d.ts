import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Scrollers wrap overflowing content in an accessible container with visual cues that help users recognize and
 *  navigate scrollable regions.
 * @documentation https://webawesome.com/docs/components/scroller
 * @status stable
 * @since 3.0
 *
 * @slot - The content to show inside the scroller.
 *
 * @cssproperty [--shadow-color=var(--wa-color-surface-default)] - The base color of the shadow.
 * @cssproperty [--shadow-size=2rem] - The size of the shadow.
 *
 * @csspart content - The container that wraps the slotted content.
 * @csspart start-shadow - The scroll shadow shown at the start edge when more content is available, unless `without-shadow` is set.
 * @csspart end-shadow - The scroll shadow shown at the end edge when more content is available, unless `without-shadow` is set.
 */
export default class WaScroller extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly localize;
    private resizeObserver;
    content: HTMLElement;
    canScroll: boolean;
    /** The scroller's orientation. */
    orientation: 'horizontal' | 'vertical';
    /** Removes the visible scrollbar. */
    withoutScrollbar: boolean;
    /** Removes the shadows. */
    withoutShadow: boolean;
    connectedCallback(): void;
    disconnectedCallback(): void;
    private handleKeyDown;
    private handleSlotChange;
    private updateScroll;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-scroller': WaScroller;
    }
}
