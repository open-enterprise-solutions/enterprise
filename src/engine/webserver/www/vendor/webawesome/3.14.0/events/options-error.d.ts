export declare class WaOptionsErrorEvent extends Event {
    readonly detail: WaOptionsErrorEventDetail;
    constructor(detail: WaOptionsErrorEventDetail);
}
interface WaOptionsErrorEventDetail {
    /** The error the data source rejected with. */
    error: unknown;
    /** The request that failed (without its abort signal). */
    request: {
        query: string;
    };
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-options-error': WaOptionsErrorEvent;
    }
}
export {};
