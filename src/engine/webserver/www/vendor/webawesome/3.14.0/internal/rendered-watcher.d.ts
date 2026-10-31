/**
 * Watches an element and reports when it stops or starts generating layout boxes, e.g. when third-party CSS applies
 * "display: none" to it or an ancestor. A ResizeObserver signals changes and getClientRects() determines the state,
 * since it distinguishes "not rendered" from "rendered at zero size". Visibility and opacity never count as hidden.
 */
export declare class RenderedWatcher {
    private readonly element;
    private readonly callback;
    private observer;
    private initialCheckHandle;
    constructor(element: HTMLElement, callback: (isRendered: boolean) => void);
    /**
     * Starts watching and reports the current state on the next frame. Additional targets also act as change signals,
     * e.g. an internal element that always has a non-zero box when rendered.
     */
    start(...additionalTargets: Element[]): void;
    /** Stops watching. */
    stop(): void;
    private check;
}
