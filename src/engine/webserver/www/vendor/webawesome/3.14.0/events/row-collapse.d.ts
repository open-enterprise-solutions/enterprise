export declare class WaRowCollapseEvent extends Event {
    readonly detail: WaRowCollapseEventDetail;
    constructor(detail: WaRowCollapseEventDetail);
}
interface WaRowCollapseEventDetail {
    row: Record<string, unknown>;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-row-collapse': WaRowCollapseEvent;
    }
}
export {};
