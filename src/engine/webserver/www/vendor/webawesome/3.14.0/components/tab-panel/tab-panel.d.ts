import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Tab panels hold the content shown for a single tab inside a tab group.
 * @documentation https://webawesome.com/docs/components/tab-panel
 * @status stable
 * @since 2.0
 *
 * @slot - The tab panel's content.
 *
 * @csspart base - Deprecated. Style the host element instead.
 *
 * @cssproperty --padding - The tab panel's padding.
 */
export default class WaTabPanel extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private readonly attrId;
    private readonly componentId;
    /** The tab panel's name. */
    name: string;
    /** When true, the tab panel will be shown. */
    active: boolean;
    role: string;
    connectedCallback(): void;
    handleActiveChange(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-tab-panel': WaTabPanel;
    }
}
