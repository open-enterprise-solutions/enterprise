export declare class WaFilterChangeEvent extends Event {
    readonly detail: WaFilterChangeEventDetail;
    constructor(detail: WaFilterChangeEventDetail);
}
interface WaFilterChangeEventDetail {
    search: string;
    filters: {
        id: string;
        value: unknown;
    }[];
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-filter-change': WaFilterChangeEvent;
    }
}
export {};
