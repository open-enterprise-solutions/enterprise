import type { PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import WaPopup from '../popup/popup.js';
/**
 * @summary Popovers display contextual content and interactive elements in a floating panel anchored to a trigger. Use
 *  them for rich tooltips, menus, or any content that appears on demand without navigating away.
 * @documentation https://webawesome.com/docs/components/popover
 * @status stable
 * @since 3.0
 *
 * @dependency wa-popup
 *
 * @slot - The popover's content. Interactive elements such as buttons and links are supported.
 *
 * @event wa-show - Emitted when the popover begins to show. Canceling this event will stop the popover from showing.
 * @event wa-after-show - Emitted after the popover has shown and all animations are complete.
 * @event wa-hide - Emitted when the popover begins to hide. Canceling this event will stop the popover from hiding.
 * @event wa-after-hide - Emitted after the popover has hidden and all animations are complete.
 *
 * @csspart dialog - The native dialog element that contains the popover content.
 * @csspart body - The popover's body where its content is rendered.
 * @csspart popup - The internal `<wa-popup>` element that positions the popover.
 * @csspart popup__popup - The popup's exported `popup` part. Use this to target the popover's popup container.
 * @csspart popup__arrow - The popup's exported `arrow` part. Use this to target the popover's arrow.
 *
 * @cssproperty [--arrow-size=0.375rem] - The size of the tiny arrow that points to the popover (set to zero to remove).
 * @cssproperty [--max-width=25rem] - The maximum width of the popover's body content.
 * @cssproperty [--show-duration=var(--wa-transition-fast)] - The speed of the show animation.
 * @cssproperty [--hide-duration=var(--wa-transition-fast)] - The speed of the hide animation.
 *
 * @cssstate open - Applied when the popover is open.
 */
export default class WaPopover extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    static dependencies: {
        'wa-popup': typeof WaPopup;
    };
    dialog: HTMLDialogElement;
    body: HTMLElement;
    popup: WaPopup;
    anchor: null | Element;
    /**
     * The preferred placement of the popover. Note that the actual placement may vary as needed to keep the popover
     * inside of the viewport.
     */
    placement: 'top' | 'top-start' | 'top-end' | 'right' | 'right-start' | 'right-end' | 'bottom' | 'bottom-start' | 'bottom-end' | 'left' | 'left-start' | 'left-end';
    /** Shows or hides the popover. */
    open: boolean;
    /** The distance in pixels from which to offset the popover away from its target. */
    distance: number;
    /** The distance in pixels from which to offset the popover along its target. */
    skidding: number;
    /** The ID of the popover's anchor element. This must be an interactive/focusable element such as a button. */
    for: string | null;
    /** Removes the arrow from the popover. */
    withoutArrow: boolean;
    private eventController;
    private pressStartedInside;
    connectedCallback(): void;
    disconnectedCallback(): void;
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    updated(changedProperties: PropertyValues<this>): void;
    private handleAnchorClick;
    private handleBodyClick;
    private handleDocumentKeyDown;
    private handleDocumentPointerDown;
    private handleDocumentClick;
    handleOpenChange(): Promise<void>;
    handleForChange(): void;
    handleOptionsChange(): Promise<void>;
    /** Shows the popover. */
    show(): Promise<void>;
    /** Hides the popover. */
    hide(): Promise<void>;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-popover': WaPopover;
    }
}
