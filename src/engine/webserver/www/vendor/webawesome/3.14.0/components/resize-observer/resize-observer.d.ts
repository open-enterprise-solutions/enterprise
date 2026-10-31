import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Resize observers watch their slotted elements for size changes and emit an event when they occur. Provides a
 *  thin, declarative interface to the browser's ResizeObserver API.
 * @documentation https://webawesome.com/docs/components/resize-observer
 * @status stable
 * @since 2.0
 *
 * @slot - One or more elements to watch for resizing.
 *
 * @event {{ entries: ResizeObserverEntry[] }} wa-resize - Emitted when the element is resized.
 */
export default class WaResizeObserver extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private resizeObserver;
    private observedElements;
    /** Disables the observer. */
    disabled: boolean;
    connectedCallback(): void;
    disconnectedCallback(): void;
    private handleSlotChange;
    private startObserver;
    private stopObserver;
    handleDisabledChange(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-resize-observer': WaResizeObserver;
    }
}
