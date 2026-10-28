import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
import '../progress-ring/progress-ring.js';
/**
 * @summary Toast items are individual notifications displayed within a toast container.
 * @documentation https://webawesome.com/docs/components/toast
 * @status stable
 * @since 3.3
 *
 * @dependency wa-icon
 * @dependency wa-progress-ring
 *
 * @slot - The toast item's message content.
 * @slot icon - An optional icon to show at the start of the toast item.
 *
 * @event wa-show - Emitted when the toast item begins to show.
 * @event wa-after-show - Emitted after the toast item has finished showing.
 * @event wa-hide - Emitted when the toast item begins to hide.
 * @event wa-after-hide - Emitted after the toast item has finished hiding.
 *
 * @csspart toast-item - The toast item's main container.
 * @csspart accent - The colored accent line on the start side.
 * @csspart icon - The icon container.
 * @csspart content - The message content container.
 * @csspart close-button - The close button element.
 * @csspart progress-ring - The progress ring component.
 * @csspart progress-ring__base - The progress ring's exported base part.
 * @csspart progress-ring__label - The progress ring's exported label part.
 * @csspart progress-ring__track - The progress ring's exported track part.
 * @csspart progress-ring__indicator - The progress ring's exported indicator part.
 * @csspart close-icon - The close icon element.
 * @csspart close-icon__svg - The close icon's exported svg part.
 *
 * @cssproperty --accent-width - The width of the accent line. Defaults to 4px.
 * @cssproperty --padding - The internal spacing of the toast item. Scales with the `size` attribute.
 * @cssproperty [--show-duration=var(--wa-transition-normal)] - The animation duration when showing.
 * @cssproperty [--hide-duration=var(--wa-transition-normal)] - The animation duration when hiding.
 */
export default class WaToastItem extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly hasSlotController;
    private readonly localize;
    private animationFrame;
    private startTime;
    private isHovering;
    private isFocused;
    toastItemElement: HTMLElement;
    /** @internal The time remaining as a percentage (100 to 0). */
    timeLeft: number;
    /** The toast item's variant. */
    variant: 'brand' | 'success' | 'warning' | 'danger' | 'neutral';
    /** The toast item's size. */
    size: 'xs' | 's' | 'm' | 'l' | 'xl' | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /**
     * The length of time in milliseconds before the toast item is automatically dismissed. Set to 0 to keep the toast
     * item open until the user dismisses it.
     */
    duration: number;
    /**
     * Only required for SSR. Set to `true` if you're slotting in an `icon` element so the server-rendered markup
     * includes the icon before the component hydrates on the client.
     */
    withIcon: boolean;
    connectedCallback(): void;
    disconnectedCallback(): void;
    /** @internal Starts the toast item's timer and shows it. Called by the parent toast component. */
    startTimer(): Promise<void>;
    /** @internal Stops the toast item's timer. */
    stopTimer(): void;
    private tick;
    /** Hides the toast item with animation and removes it from the DOM. */
    hide(): Promise<void>;
    private handleCloseClick;
    private pauseTimer;
    private resumeTimer;
    private handlePointerEnter;
    private handlePointerLeave;
    private handleFocusIn;
    private handleFocusOut;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-toast-item': WaToastItem;
    }
}
