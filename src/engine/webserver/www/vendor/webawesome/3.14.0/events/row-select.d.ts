export declare class WaRowSelectEvent extends Event {
    readonly detail: WaRowSelectEventDetail;
    constructor(detail: WaRowSelectEventDetail);
}
interface WaRowSelectEventDetail {
    selectedKeys: (string | number)[];
    selectedRows: Record<string, unknown>[];
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-row-select': WaRowSelectEvent;
    }
}
export {};
