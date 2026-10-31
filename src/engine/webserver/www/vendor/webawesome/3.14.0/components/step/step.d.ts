import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
import '../spinner/spinner.js';
/**
 * @summary Steps represent a single stage inside a `<wa-stepper>`, showing its position, label, and status.
 * @documentation https://webawesome.com/docs/components/step
 * @status experimental
 * @since 3.14
 *
 * @dependency wa-icon
 * @dependency wa-spinner
 *
 * @slot - The step's label.
 * @slot description - Optional text shown under the label.
 * @slot icon - An element, such as `<wa-icon>`, that replaces the step number, checkmark, or loading indicator.
 *
 * @csspart step - The component's outer wrapper.
 * @csspart connector - The line connecting this step to its neighbors. Each step draws the half leading in and the
 *  half leading out, and both carry this part name.
 * @csspart button - The `<button>` wrapping the marker and content. Only rendered when the parent stepper is
 *  `clickable`; otherwise the same wrapper is a plain, non-focusable element.
 * @csspart marker - The circular marker that shows the step's number, checkmark, or loading indicator.
 * @csspart spinner - The spinner shown in the marker while the step is `loading`.
 * @csspart content - The wrapper around the label, status text, and description.
 * @csspart label - The step's label.
 * @csspart status - Visually hidden text that tells assistive technology whether the step is completed, not
 *  completed, or locked.
 * @csspart description - The step's description.
 *
 * @cssproperty --pulse-color - The color of the marker's pulse effect when using `attention="pulse"`. Defaults to the
 *  step's accent color.
 *
 * @cssstate active - Applied when this is the parent stepper's current step.
 * @cssstate completed - Mirrors the `completed` attribute.
 * @cssstate loading - Mirrors the `loading` attribute.
 * @cssstate disabled - Mirrors the `disabled` attribute.
 * @cssstate locked - Applied by the parent stepper when `linear` is set and this step can't be reached yet, i.e. it
 *  comes after the first incomplete step.
 * @cssstate clickable - Applied by the parent stepper when its `clickable` attribute is set, allowing this step to
 *  be clicked or activated (Enter/Space) directly.
 */
export default class WaStep extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly localize;
    private readonly hasSlotController;
    /** Identifies the step. Matched against the stepper's `active` attribute and used in events. */
    name: string;
    /** Marks the step done. Shows a checkmark instead of the step number. */
    completed: boolean;
    /** Shows a loading indicator instead of the step number, e.g. while an async transition is in progress. */
    loading: boolean;
    /**
     * Makes the step non-interactive. It can't be clicked or reached with `next()`/`goTo()`, and it renders as a
     * disabled button when the stepper is `clickable`.
     */
    disabled: boolean;
    /**
     * Colors the step's marker with a semantic color. Upcoming `brand` steps keep a neutral outline so a default stepper
     * reads quietly. The color is cosmetic; pair it with an icon in the `icon` slot and a clear label when a step needs
     * to read as failed or flagged.
     */
    variant: 'neutral' | 'brand' | 'success' | 'warning' | 'danger';
    /** Adds an animation to the step's marker to draw attention to it, e.g. the step the user should do next. */
    attention: 'none' | 'pulse' | 'bounce';
    /**
     * Only required for SSR. Set to `true` if you're slotting in a `description` element, so the server-rendered
     * markup includes it before the component hydrates on the client.
     */
    withDescription: boolean;
    /**
     * @internal Set by the parent `<wa-stepper>` to this step's 1-based position among its siblings. Used as the
     * default marker content. Stays 0 until the stepper sets it, e.g. during SSR, which leaves the marker empty.
     */
    position: number;
    /**
     * Draws the step as the stepper's current step. The parent `<wa-stepper>` sets this from its own `active`
     * attribute, so you only need to set it yourself for SSR.
     */
    active: boolean;
    /**
     * @internal Set by the parent `<wa-stepper>` to match its own `clickable` attribute. Renders the step's marker and
     * content inside a `<button>` when set.
     */
    clickable: boolean;
    /**
     * @internal Set by the parent `<wa-stepper>`. True for every step that can't be reached yet when the stepper's
     * `linear` attribute is set.
     */
    locked: boolean;
    /**
     * @internal Set by the parent `<wa-stepper>`. The variant of the completed step before this one, so the
     * half-connector leading in matches the half leading out of it. Unset when the previous step isn't completed.
     */
    connectorStartVariant?: WaStep['variant'];
    role: string;
    handleCompletedChange(): void;
    handleLoadingChange(): void;
    handleDisabledChange(): void;
    handleActiveChange(): void;
    handleLockedChange(): void;
    private syncAriaCurrent;
    private getStatusText;
    handleClickableChange(): void;
    private renderIcon;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-step': WaStep;
    }
}
