export declare class WaFocusDayEvent extends Event {
    readonly detail: WaFocusDayEventDetail;
    constructor(detail: WaFocusDayEventDetail);
}
interface WaFocusDayEventDetail {
    date: Date;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-focus-day': WaFocusDayEvent;
    }
}
export {};
