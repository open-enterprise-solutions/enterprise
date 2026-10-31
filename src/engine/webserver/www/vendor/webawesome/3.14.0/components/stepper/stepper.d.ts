import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../step/step.js';
/**
 * @summary Steppers visually guide users through a process step by step, breaking content into clear, logical
 *  stages. Use them for checkout flows, multi-step setup, onboarding, or just to show the status of a process.
 * @documentation https://webawesome.com/docs/components/stepper
 * @status experimental
 * @since 3.14
 *
 * @dependency wa-step
 *
 * @event {{ name: string, previousName: string | null, step: WaStep, previousStep: WaStep | null }} wa-before-step-change
 *  - Emitted before the active step
 *  changes. Calling `event.preventDefault()` prevents the change, to guard against invalid or unsaved data.
 * @event {{ name: string, previousName: string | null, step: WaStep, previousStep: WaStep | null }} wa-step-change
 *  - Emitted after the active step changes.
 *
 * @slot - One or more `<wa-step>` elements.
 *
 * @csspart stepper - The component's outer wrapper. A `<nav>` landmark when the stepper is `clickable`, since its
 *  steps are then controls you can navigate with; otherwise a labeled `role="group"`, since a display-only stepper has
 *  nothing to navigate.
 * @csspart summary - Visually hidden "Step X of Y" text that tells assistive technology where the active step sits.
 * @csspart steps - The `<ol>` that lays out the steps.
 *
 * @cssproperty [--gap=var(--wa-space-l)] - The space between steps.
 * @cssproperty [--marker-size=2em] - The size of each step's marker.
 * @cssproperty [--connector-color=var(--wa-color-neutral-fill-normal)] - The color of the connector line after a
 *  step that isn't completed.
 * @cssproperty --connector-color-active - The color of the connector line after a completed step. Unset by default,
 *  so the line takes the completed marker's fill and follows its `variant`.
 * @cssproperty [--connector-width=var(--wa-border-width-m)] - The thickness of the connector line, in either
 *  orientation.
 * @cssproperty [--connector-gap=0.35em] - The gap between a marker's edge and the connector line, on both sides.
 *  Kept clear of the marker geometrically, so it holds even if a marker's background is transparent.
 *
 * @cssstate completed - Applied when every step is completed.
 * @cssstate loading - Applied when at least one step is loading.
 * @cssstate stacked - Applied while the steps are laid out vertically, whether by `orientation="vertical"` or because
 *  an `auto` stepper is too narrow to give each step room.
 *
 * @ssr - During SSR, `<wa-stepper>` can't access its children to determine which step is active. To render the correct
 *  step, also set the `active` attribute on the matching `<wa-step>`. Step numbers appear once the stepper hydrates.
 */
export default class WaStepper extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly localize;
    private mutationObserver?;
    private resizeObserver?;
    stepsEl: HTMLOListElement;
    defaultSlot: HTMLSlotElement;
    private isStacked;
    private activeIndex;
    private stepCount;
    /** The name of the active step. Falls back to the first step if unset, or if it doesn't match any step's name. */
    active: string;
    /**
     * The stepper's layout direction. `auto` lays steps out in a row and stacks them when the stepper is too narrow to
     * give each step about 6em of width, so labels stay legible on small screens; use it for anything shown on a
     * phone. It relies on measuring the stepper, so a server-rendered `auto` stepper starts as a row and stacks once
     * it hydrates, which is why `horizontal` is the default.
     */
    orientation: 'horizontal' | 'vertical' | 'auto';
    /**
     * Requires steps to be completed in order. When set, `next()`/`goTo()`/a `data-stepper` invoker and, if
     * `clickable` is also set, clicking or activating a step can't reach a step until every step before it is
     * completed. Every step past that point renders as locked.
     */
    linear: boolean;
    /**
     * Allows clicking a step, or focusing it and pressing Enter/Space, to jump straight to it. When unset (the
     * default), only `next()`/`previous()`/`goTo()` change the active step, e.g. from your own Next/Back buttons or a
     * `data-stepper` invoker.
     */
    clickable: boolean;
    /** A label that describes the stepper to assistive devices. Especially useful when more than one is on the page. */
    label: string;
    constructor();
    connectedCallback(): void;
    disconnectedCallback(): void;
    private handleSlotChange;
    private getAllSteps;
    /** In `linear` mode, the 0-based index of the boundary a step's position must fall within (inclusive) to be reachable. */
    private getLinearBoundaryIndex;
    private isReachable;
    /** Recomputes each step's position/active/locked state and the stepper's own custom states. */
    private syncSteps;
    handleStateChange(): void;
    /**
     * Decides whether an `auto` stepper stacks at the given width. Each step needs roughly 6em to keep a short label
     * on one line, so the row stacks once the stepper is narrower than that times the step count, plus the gaps.
     */
    private updateStacking;
    /**
     * Requests a change to the named step. Emits a cancelable `wa-before-step-change`; if not canceled, updates
     * `active`, emits `wa-step-change`, and announces the new position to assistive technology. No-ops silently if the
     * step doesn't exist, is disabled, or (in `linear` mode) isn't reachable yet.
     */
    goTo(name: string): void;
    /** Announces the active step's position to assistive technology via the shared light-DOM live region. */
    private announceActiveStep;
    /** Advances to the step after the active one, if any. */
    next(): void;
    /** Goes back to the step before the active one, if any. */
    previous(): void;
    private handleClick;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-stepper': WaStepper;
    }
}
