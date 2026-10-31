import type { ReactiveController, ReactiveControllerHost } from 'lit';
/** A reactive controller that determines when slots exist. */
export declare class HasSlotController implements ReactiveController {
    host: ReactiveControllerHost & HTMLElement;
    slotNames: string[];
    constructor(host: typeof this.host, ...slotNames: string[]);
    private hasDefaultSlot;
    private hasNamedSlot;
    /**
     * @param slotName     - Name of the slot to look for
     * @param propertyName - Generally we infer via `withHeader` property on the host, but in cases where its different, you can specify a manual property name.
     */
    test(slotName: string, propertyName?: null | string): boolean;
    hostConnected(): void;
    hostDisconnected(): void;
    private handleSlotChange;
}
/**
 * Given a list of nodes, this function iterates over all of them and returns the concatenated
 * HTML as a string. This is useful for getting the HTML that corresponds to a slot’s assigned nodes (since we can't use slot.innerHTML as an alternative).
 * @param nodes - The list of nodes to iterate over.
 * @param callback - A function that can be used to customize the HTML output for specific types of nodes. If the function returns undefined, the default HTML output will be used.
 */
export declare function getInnerHTML(nodes: Iterable<Node>, callback?: (node: Node) => string | undefined): string;
