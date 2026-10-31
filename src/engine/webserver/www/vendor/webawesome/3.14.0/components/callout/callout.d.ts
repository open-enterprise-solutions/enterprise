import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Callouts display important messages inline with surrounding content. Use them to highlight tips, warnings,
 *  errors, or other information users should not miss.
 * @documentation https://webawesome.com/docs/components/callout
 * @status stable
 * @since 3.0
 *
 * @slot - The callout's main content.
 * @slot icon - An icon to show in the callout. Works best with `<wa-icon>`.
 *
 * @csspart icon - The container that wraps the optional icon.
 * @csspart message - The container that wraps the callout's main content.
 */
export default class WaCallout extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    /** The callout's theme variant. Defaults to `brand` if not within another element with a variant. */
    variant: 'brand' | 'neutral' | 'success' | 'warning' | 'danger';
    /** The callout's visual appearance. */
    appearance: 'accent' | 'filled' | 'outlined' | 'plain' | 'filled-outlined';
    /** The callout's size. */
    size: 'xs' | 's' | 'm' | 'l' | 'xl' | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-callout': WaCallout;
    }
}
