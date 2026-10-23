export declare class WaPageChangeEvent extends Event {
    readonly detail: WaPageChangeEventDetail;
    constructor(detail: WaPageChangeEventDetail);
}
interface WaPageChangeEventDetail {
    page: number;
    pageSize: number;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-page-change': WaPageChangeEvent;
    }
}
export {};
