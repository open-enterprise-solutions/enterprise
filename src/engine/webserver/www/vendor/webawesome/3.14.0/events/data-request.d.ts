export declare class WaDataRequestEvent extends Event {
    readonly detail: WaDataRequestEventDetail;
    constructor(detail: WaDataRequestEventDetail);
}
interface WaDataRequestEventDetail {
    sort: {
        id: string;
        desc: boolean;
    }[];
    filters: {
        id: string;
        value: unknown;
    }[];
    search: string;
    page: number;
    pageSize: number;
    signal: AbortSignal;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-data-request': WaDataRequestEvent;
    }
}
export {};
