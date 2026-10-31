import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
/**
 * @summary Pagination splits long lists of content into pages, letting users navigate between them.
 * @documentation https://webawesome.com/docs/components/pagination
 * @status stable
 * @since 3.11
 *
 * @dependency wa-icon
 *
 * @event wa-before-page-change - Emitted when the page is about to change but before it does. Canceling this event with
 *  `event.preventDefault()` prevents the page from changing.
 * @event wa-page-change - Emitted after the page changes.
 *
 * @slot previous-icon - An icon to use in lieu of the default previous icon.
 * @slot next-icon - An icon to use in lieu of the default next icon.
 * @slot first-icon - An icon to use in lieu of the default first icon.
 * @slot last-icon - An icon to use in lieu of the default last icon.
 *
 * @csspart base - Deprecated. Use the `pagination` part instead.
 * @csspart pagination - The component's outer wrapper, a `<nav>` element.
 * @csspart button - Every button or link, including page numbers and navigation controls.
 * @csspart previous-button - The previous button.
 * @csspart next-button - The next button.
 * @csspart first-button - The first button.
 * @csspart last-button - The last button.
 * @csspart pages - The list that wraps the page number items.
 * @csspart page - A page number button or link.
 * @csspart page-current - The current page number button or link.
 * @csspart ellipsis - An ellipsis for collapsed pages. Acts as a button that jumps several pages toward that side.
 * @csspart summary - The summary of items on the current page, shown with the `with-summary` attribute.
 * @csspart label - The "1 of 5" label shown between the navigation buttons in the `compact` layout.
 *
 * @cssstate disabled - Applied when the pagination is disabled.
 */
export default class WaPagination extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    /** How many pages an ellipsis skips when clicked. Matches the common convention (e.g. Ant Design). */
    private static readonly jumpDistance;
    private readonly localize;
    /** The total number of items to paginate. */
    total: number;
    /** The number of items shown per page. */
    pageSize: number;
    /** The current page, starting at 1. */
    page: number;
    /** The number of pages to show on each side of the current page. */
    siblingCount: number;
    /** The number of pages to always show at the start and end. */
    boundaryCount: number;
    /** Hides the previous and next buttons. */
    withoutNav: boolean;
    /** Shows buttons that jump to the first and last pages. */
    withEdges: boolean;
    /** Shows a summary of the items on the current page, e.g. "1–10 of 237". */
    withSummary: boolean;
    /**
     * The pagination's layout. The default `standard` format shows the full page list with ellipses; `compact` collapses
     * it into a short "1 of 5" label flanked by the previous and next buttons, useful in tight spaces like toolbars and
     * cards.
     */
    format: 'standard' | 'compact';
    /**
     * A URL template used to render page items as links instead of buttons. When set, items render as `<a>` elements for
     * SSR, SEO, and no-JS support. Provide a string with `{page}` as a placeholder for the page number, e.g.
     * `/products?page={page}`. In JavaScript, you can also assign a function that receives the page number and returns
     * the URL, e.g. `el.hrefTemplate = page => \`/products?page=${page}\``.
     */
    hrefTemplate: string | ((page: number) => string);
    /** Renders nothing when there's only one page. */
    hideSinglePage: boolean;
    /**
     * A label that describes the pagination to assistive devices. This won't be shown on the screen, but it will be
     * announced by screen readers. Especially useful when more than one pagination control exists on the same page.
     */
    label: string;
    /** The pagination's visual appearance. */
    appearance: 'outlined' | 'filled' | 'plain';
    /** Disables the pagination. */
    disabled: boolean;
    /** Tracks whether the most recent page change came from an in-component activation, so we can restore focus. */
    private shouldRestoreFocus;
    /** The total number of pages, derived from `total` and `pageSize`. */
    get totalPages(): number;
    handleDisabledChange(): void;
    handlePageBoundsChange(): void;
    /** Resolves the URL for a given page when `hrefTemplate` is set, supporting both the string and function forms. */
    private getHref;
    /**
     * Requests a change to the given page. Emits a cancelable `wa-before-page-change`; if not canceled, updates `page`,
     * restores focus to the new current page, emits `wa-page-change`, and announces the change.
     */
    private requestPage;
    /** Moves focus to the current page item after an in-component activation, so focus never falls back to the body. */
    private restoreFocusToCurrentPage;
    /** Announces the current page to assistive technology via the shared light-DOM live region. */
    private announcePage;
    updated(): void;
    /** Renders a navigation control (first/previous/next/last). */
    private renderNavButton;
    /** Renders a single numbered page item. The visible number is the accessible name — no redundant aria-label. */
    private renderPage;
    /**
     * Renders an ellipsis for a run of collapsed pages. Clicking it jumps a fixed number of pages toward that side, so a
     * `start` ellipsis jumps backward and an `end` ellipsis jumps forward. `data-ellipsis` lets the styles collapse a
     * second ellipsis when space is tight.
     */
    private renderEllipsis;
    render(): import("lit-html").TemplateResult<1>;
    /** Renders the optional "1–10 of 237" summary. */
    private renderSummary;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-pagination': WaPagination;
    }
}
