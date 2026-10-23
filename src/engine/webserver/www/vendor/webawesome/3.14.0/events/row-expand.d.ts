export declare class WaRowExpandEvent extends Event {
    readonly detail: WaRowExpandEventDetail;
    constructor(detail: WaRowExpandEventDetail);
}
interface WaRowExpandEventDetail {
    row: Record<string, unknown>;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-row-expand': WaRowExpandEvent;
    }
}
export {};
