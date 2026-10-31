import { type PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Selects one or more child elements at random and displays them, hiding the rest.
 * @documentation https://webawesome.com/docs/components/random-content
 * @status stable
 * @since 3.10
 *
 * @slot - The pool of children to choose from. Only direct element children are eligible; unselected
 *  children are hidden with the `hidden` attribute.
 *
 * @event {{ items: Element[] }} wa-content-change - Emitted whenever the displayed selection changes,
 *  including on first render, on `randomize()`, and on each autoplay tick.
 *
 * @cssproperty --animation-duration - Duration of the entrance animation. Default is `300ms`.
 * @cssproperty --animation-easing - Easing function for the entrance animation. Default is `ease`.
 * @cssproperty --animation-translate - Translation distance for directional animations (`fade-up`, `fade-down`, `fade-left`, `fade-right`). Default is `0.5em`.
 */
export default class WaRandomContent extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private sequenceCursor;
    private uniqueQueue;
    private currentSelection;
    private isInitialSelection;
    private autoplayController;
    private animationCleanups;
    /** Text pushed to a visually-hidden live region so screen readers hear rotations. */
    private liveAnnouncement;
    /** Number of children to show simultaneously. Clamped to [1, childCount]. */
    items: number;
    /** Selection strategy: `unique` (default), `random`, or `sequence`. */
    mode: 'random' | 'unique' | 'sequence';
    /** Rotate the content automatically. Set the cadence with `autoplay-interval`. */
    autoplay: boolean;
    /** Autoplay cadence in milliseconds. */
    autoplayInterval: number;
    /** Entrance animation for newly shown children. */
    animation: 'none' | 'fade' | 'fade-up' | 'fade-down' | 'fade-left' | 'fade-right';
    connectedCallback(): void;
    firstUpdated(changedProperties: PropertyValues<this>): void;
    handleAutoplayChange(): void;
    handleModeChange(): void;
    handleItemsChange(): void;
    /** Selects a new set of children using the current mode. Returns the elements now shown. */
    randomize(): Element[];
    private syncAutoplay;
    private assignedChildren;
    private sample;
    private handleSlotChange;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-random-content': WaRandomContent;
    }
}
