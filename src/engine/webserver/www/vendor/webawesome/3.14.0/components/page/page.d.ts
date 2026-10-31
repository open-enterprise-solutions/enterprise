import type { PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../button/button.js';
import '../drawer/drawer.js';
import type WaDrawer from '../drawer/drawer.js';
import '../icon/icon.js';
/**
 * @summary Pages scaffold an entire application layout with header, navigation, sidebar, main content, aside, and
 *  footer regions. Use them to structure full pages with minimal markup and responsive behavior built in.
 * @documentation https://webawesome.com/docs/components/page
 * @status stable
 * @since 3.0
 *
 * @slot - The page's main content.
 * @slot banner - The banner that gets display above the header. The banner will not be shown if no content is provided.
 * @slot header - The header to display at the top of the page. If a banner is present, the header will appear below the banner. The header will not be shown if there is no content.
 * @slot subheader - A subheader to display below the `header`. This is a good place to put things like breadcrumbs.
 * @slot menu - The left side of the page. If you slot an element in here, you will override the default `navigation` slot and will be handling navigation on your own. This also will not disable the fallback behavior of the navigation button. This section "sticks" to the top as the page scrolls.
 * @slot navigation-header - The header for a navigation area. On mobile this will be the header for `<wa-drawer>`.
 * @slot navigation - The main content to display in the navigation area. This is displayed on the left side of the page, if `menu` is not used. This section "sticks" to the top as the page scrolls.
 * @slot navigation-footer - The footer for a navigation area. On mobile this will be the footer for `<wa-drawer>`.
 * @slot navigation-toggle - Use this slot to slot in your own button + icon for toggling the navigation drawer. By default it is a `<wa-button>` + a 3 bars `<wa-icon>`
 * @slot navigation-toggle-icon - Use this to slot in your own icon for toggling the navigation drawer. By default it is 3 bars `<wa-icon>`.
 * @slot main-header - Header to display inline above the main content.
 * @slot main-footer - Footer to display inline below the main content.
 * @slot aside - Content to be shown on the right side of the page. Typically contains a table of contents, ads, etc. This section "sticks" to the top as the page scrolls.
 * @slot skip-to-content - The "skip to content" slot. You can override this If you would like to override the `Skip to content` button and add additional "Skip to X", they can be inserted here.
 * @slot footer - The content to display in the footer. This is always displayed underneath the viewport so will always make the page "scrollable".
 *
 * @csspart base - Deprecated. Use the `page` part instead.
 * @csspart page - The component's outer wrapper.
 * @csspart banner - The banner to show above header.
 * @csspart header - The header, usually for top level navigation / branding.
 * @csspart subheader - Shown below the header, usually intended for things like breadcrumbs and other page level navigation.
 * @csspart body - The wrapper around menu, main, and aside.
 * @csspart menu - The left hand side of the page. Generally intended for navigation.
 * @csspart navigation - The `<nav>` that wraps the navigation slots on desktop viewports.
 * @csspart navigation-desktop - The `<nav>` for navigation on desktop viewports.
 * @csspart navigation-header - The header for a navigation area. On mobile this will be the header for `<wa-drawer>`.
 * @csspart navigation-footer - The footer for a navigation area. On mobile this will be the footer for `<wa-drawer>`.
 * @csspart navigation-toggle - The default `<wa-button>` that will toggle the `<wa-drawer>` for mobile viewports.
 * @csspart navigation-toggle-icon - The default `<wa-icon>` displayed inside of the navigation-toggle button.
 * @csspart drawer - The `<wa-drawer>` that contains the navigation on mobile viewports.
 * @csspart main - The wrapper around the main header, content, and footer.
 * @csspart main-header - The header above main content.
 * @csspart main-content - The main content.
 * @csspart main-footer - The footer below main content.
 * @csspart aside - The right hand side of the page. Used for things like table of contents, ads, etc.
 * @csspart skip-to-content - The "skip to content" link that lets keyboard users bypass navigation.
 * @csspart footer - The footer of the page. This is always below the initial viewport size.
 *
 * @cssproperty [--menu-width=auto] - The width of the page's "menu" section.
 * @cssproperty [--main-width=1fr] - The width of the page's "main" section.
 * @cssproperty [--aside-width=auto] - The wide of the page's "aside" section.
 * @cssproperty [--banner-height=0px] - The height of the banner. This gets calculated when the page initializes. If the height is known, you can set it here to prevent shifting when the page loads.
 * @cssproperty [--header-height=0px] - The height of the header. This gets calculated when the page initializes. If the height is known, you can set it here to prevent shifting when the page loads.
 * @cssproperty [--subheader-height=0px] - The height of the subheader. This gets calculated when the page initializes. If the height is known, you can set it here to prevent shifting when the page loads.
 */
export default class WaPage extends WebAwesomeElement {
    static css: import("lit").CSSResult[];
    private readonly hasSlotController;
    private headerResizeObserver;
    private subheaderResizeObserver;
    private bannerResizeObserver;
    private footerResizeObserver;
    private hasExplicitNavigationToggleSetting;
    private slotResizeObserver;
    private handleNavigationToggle;
    header: HTMLElement;
    menu: HTMLElement;
    main: HTMLElement;
    aside: HTMLElement;
    subheader: HTMLElement;
    footer: HTMLElement;
    banner: HTMLElement;
    navigationDrawer: WaDrawer;
    navigationToggleSlot: HTMLSlotElement;
    /**
     * The view is a reflection of the "mobileBreakpoint", when the page is larger than the `mobile-breakpoint` (768px by
     * default), it is considered to be a "desktop" view. The view is merely a way to distinguish when to show/hide the
     * navigation. You can use additional media queries to make other adjustments to content as necessary.
     * The default is "desktop" because the "mobile navigation drawer" isn't accessible via SSR due to drawer requiring JS.
     */
    view: 'mobile' | 'desktop';
    /**
     * Sets a `nonce` for the injected `<style>` tag used for handling media queries. The style tag will use `window.litNonce` if `nonce` isn't defined on `<wa-page>`
     */
    nonce: string | undefined;
    /**
     * Whether or not the navigation drawer is open. Note, the navigation drawer is only "open" on mobile views.
     */
    navOpen: boolean;
    /**
     * At what page width to hide the "navigation" slot and collapse into a hamburger button.
     * Accepts both numbers (interpreted as px) and CSS lengths (e.g. `50em`), which are resolved based on the root element.
     */
    mobileBreakpoint: string;
    /**
     * Where to place the navigation when in the mobile viewport.
     */
    navigationPlacement: 'start' | 'end';
    /**
     * Determines whether or not to hide the default hamburger button. This will automatically flip to "true" if you add
     * an element with `data-toggle-nav` anywhere in the element light DOM. Generally this will be set for you and you
     * don't need to do anything, unless you're using SSR, in which case you should set this manually for initial page
     * loads. If you set this yourself, automatic detection is skipped. This is useful when your toggle lives inside
     * another component's shadow root, where it can't be detected.
     */
    disableNavigationToggle: boolean;
    pageResizeObserver: ResizeObserver | null;
    private updateNavigationToggleState;
    protected updated(changedProperties: PropertyValues<this>): void;
    constructor();
    connectedCallback(): void;
    /**
     * https://stackoverflow.com/a/26831113
     * This prevents awkward gaps when scrolling the page and the aside / menu don't "fill" the gaps.
     */
    visiblePixelsInViewport(element: HTMLElement | null): number | null;
    firstUpdated(changedProperties: PropertyValues<typeof this>): void;
    disconnectedCallback(): void;
    /**
     * Shows the mobile navigation drawer
     */
    showNavigation(): void;
    /**
     * Hides the mobile navigation drawer
     */
    hideNavigation(): void;
    /**
     * Toggles the mobile navigation drawer
     */
    toggleNavigation(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-page': WaPage;
    }
}
