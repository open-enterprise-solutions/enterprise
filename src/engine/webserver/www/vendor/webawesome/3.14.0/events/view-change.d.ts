export declare class WaViewChangeEvent extends Event {
    readonly detail: WaViewChangeEventDetail;
    constructor(detail: WaViewChangeEventDetail);
}
interface WaViewChangeEventDetail {
    view: 'days' | 'months' | 'years';
    date: Date;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-view-change': WaViewChangeEvent;
    }
}
export {};
