import type { PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Progress bars show how far along an ongoing operation is as a horizontal fill. Use them for file uploads,
 *  multi-step flows, or any task with measurable progress.
 * @documentation https://webawesome.com/docs/components/progress-bar
 * @status stable
 * @since 2.0
 *
 * @slot - A label to show inside the progress indicator.
 *
 * @csspart base - Deprecated. Use the `progress-bar` part instead.
 * @csspart progress-bar - The component's outer wrapper.
 * @csspart indicator - The progress bar's indicator.
 * @csspart label - The progress bar's label.
 *
 * @cssproperty [--track-height=1rem] - The height of the track.
 * @cssproperty [--track-color=var(--wa-color-neutral-fill-normal)] - The color of the track.
 * @cssproperty [--indicator-color=var(--wa-color-brand-fill-loud)] - The color of the indicator.
 */
export default class WaProgressBar extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private readonly localize;
    /** The current progress as a percentage, 0 to 100. */
    value: number;
    /** When true, percentage is ignored, the label is hidden, and the progress bar is drawn in an indeterminate state. */
    indeterminate: boolean;
    /** A custom label for assistive devices. */
    label: string;
    willUpdate(changedProperties: PropertyValues<this>): void;
    updated(changedProperties: PropertyValues<this>): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-progress-bar': WaProgressBar;
    }
}
