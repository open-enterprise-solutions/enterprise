import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Fetches an external HTML file and embeds its contents inline on the page. Useful for reusing shared markup
 *  like headers, footers, and partials across multiple pages.
 * @documentation https://webawesome.com/docs/components/include
 * @status stable
 * @since 2.0
 *
 * @event wa-load - Emitted when the included file is loaded.
 * @event {{ status: number }} wa-include-error - Emitted when the included file fails to load due to an error.
 *
 * @ssr - `<wa-include>` fetches its content asynchronously (like `<wa-icon>`), so the rendered output isn't available during SSR.
 */
export default class WaInclude extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    /**
     * The location of the content to include. This can be a URL to an HTML file, a same-page reference to an element's id
     * (e.g. `#my-id`), or a URL with a fragment that targets an element's id within the fetched file
     * (e.g. `/partials.html#my-id`). When targeting an element by id, its content is cloned. If the target is a
     * `<template>`, its child nodes are cloned. Be sure you trust the content you are including as it will be executed as
     * code and can result in XSS attacks.
     */
    src: string;
    /** The fetch mode to use. */
    mode: 'cors' | 'no-cors' | 'same-origin';
    /**
     * Allows included scripts to be executed. Be sure you trust the content you are including as it will be executed as
     * code and can result in XSS attacks.
     */
    allowScripts: boolean;
    private executeScript;
    /** Clones the contents of an element — a template's `content`, or any other element's children — for insertion. */
    private cloneFragment;
    private childNodesToFragment;
    handleSrcChange(): Promise<void>;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-include': WaInclude;
    }
}
