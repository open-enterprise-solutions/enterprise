import { type PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
/**
 * @summary Accordion items are used inside `<wa-accordion>` to create expandable sections with accessible headers.
 * @documentation https://webawesome.com/docs/components/accordion
 * @status stable
 * @since 3.8
 *
 * @dependency wa-icon
 *
 * @slot - The accordion item's body content.
 * @slot label - The accordion item's label. Alternatively, use the `label` attribute.
 * @slot icon - Optional expand/collapse icon. Works best with `<wa-icon>`.
 *
 * @csspart base - Deprecated. Use the `accordion-item` part instead.
 * @csspart accordion-item - The component's outer wrapper.
 * @csspart heading - The heading element wrapping the trigger button. Omitted when `heading-level="none"`.
 * @csspart button - The trigger button that toggles the panel.
 * @csspart label - The container that wraps the label.
 * @csspart icon - The container that wraps the expand/collapse icon.
 * @csspart panel - The panel that contains the item's content.
 * @csspart content - The content slot inside the panel.
 *
 * @cssproperty [--spacing=var(--wa-space-m)] - The amount of space around and between the item's header and content.
 * @cssproperty [--show-duration=var(--wa-transition-normal)] - The duration of the expand animation.
 * @cssproperty [--hide-duration=var(--wa-transition-normal)] - The duration of the collapse animation.
 * @cssproperty [--easing=var(--wa-transition-easing)] - The easing of the expand/collapse animation.
 *
 * @cssstate animating - Applied while the panel is animating.
 */
export default class WaAccordionItem extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private animationGeneration;
    private body;
    private triggerButton;
    private readonly localize;
    private isAnimating;
    /** The text label shown in the header. If you need HTML, use the `label` slot instead. */
    label: string;
    /** Expands the accordion item. */
    expanded: boolean;
    /** Disables the accordion item so it can't be toggled. */
    disabled: boolean;
    /** @internal Set by the parent accordion to control the heading level of the trigger. */
    headingLevel: string;
    /** @internal Set by the parent accordion to control icon placement. */
    iconPlacement: 'start' | 'end';
    /** @internal Set by the parent accordion to control the visual appearance. */
    appearance: 'filled' | 'outlined' | 'filled-outlined' | 'plain';
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    updated(): void;
    private handleTriggerClick;
    private handleTriggerKeyDown;
    handleExpandedChange(): Promise<void>;
    /** Expands the accordion item with animation. */
    expand(): Promise<void>;
    /** Collapses the accordion item with animation. */
    collapse(): Promise<void>;
    /** Toggles the accordion item's expanded state. */
    toggle(): Promise<void>;
    /** Focuses the accordion item's trigger button. */
    focus(options?: FocusOptions): void;
    private renderHeadingWrapper;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-accordion-item': WaAccordionItem;
    }
}
