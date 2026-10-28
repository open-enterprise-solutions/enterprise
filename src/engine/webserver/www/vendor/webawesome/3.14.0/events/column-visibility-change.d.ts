export declare class WaColumnVisibilityChangeEvent extends Event {
    readonly detail: WaColumnVisibilityChangeEventDetail;
    constructor(detail: WaColumnVisibilityChangeEventDetail);
}
interface WaColumnVisibilityChangeEventDetail {
    /** The id of the column whose visibility changed. */
    column: string;
    /** Whether the column is now visible. */
    visible: boolean;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-column-visibility-change': WaColumnVisibilityChangeEvent;
    }
}
export {};
