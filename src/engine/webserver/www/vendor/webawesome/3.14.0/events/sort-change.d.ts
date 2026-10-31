export declare class WaSortChangeEvent extends Event {
    readonly detail: WaSortChangeEventDetail;
    constructor(detail: WaSortChangeEventDetail);
}
interface WaSortChangeEventDetail {
    sort: {
        id: string;
        desc: boolean;
    }[];
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-sort-change': WaSortChangeEvent;
    }
}
export {};
