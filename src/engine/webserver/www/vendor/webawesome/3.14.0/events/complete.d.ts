export declare class WaCompleteEvent extends Event {
    constructor();
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-complete': WaCompleteEvent;
    }
}
