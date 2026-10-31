export declare class WaColumnResizeEvent extends Event {
    readonly detail: WaColumnResizeEventDetail;
    constructor(detail: WaColumnResizeEventDetail);
}
interface WaColumnResizeEventDetail {
    /** The id of the column being resized. */
    column: string;
    /** The column's new width in pixels. */
    width: number;
    /** `false` during a live drag; `true` once the drag (or keyboard resize) settles. Persist on `true`. */
    finished: boolean;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-column-resize': WaColumnResizeEvent;
    }
}
export {};
