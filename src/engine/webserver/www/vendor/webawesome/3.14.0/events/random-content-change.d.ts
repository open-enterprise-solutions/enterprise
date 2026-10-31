export declare class WaContentChangeEvent extends Event {
    readonly detail: WaContentChangeEventDetails;
    constructor(detail: WaContentChangeEventDetails);
}
interface WaContentChangeEventDetails {
    /** The elements currently shown after the selection. */
    items: Element[];
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-content-change': WaContentChangeEvent;
    }
}
export {};
