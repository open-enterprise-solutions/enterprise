export declare class WaColumnPinEvent extends Event {
    readonly detail: WaColumnPinEventDetail;
    constructor(detail: WaColumnPinEventDetail);
}
interface WaColumnPinEventDetail {
    /** The id of the column whose pin state changed. */
    column: string;
    /** The side the column is now pinned to, or `false` when unpinned. */
    side: 'left' | 'right' | false;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-column-pin': WaColumnPinEvent;
    }
}
export {};
