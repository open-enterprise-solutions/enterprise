import type WaAccordionItem from '../components/accordion-item/accordion-item.js';
export declare class WaAccordionItemTriggerEvent extends Event {
    readonly detail: {
        item: WaAccordionItem;
    };
    constructor(detail: {
        item: WaAccordionItem;
    });
}
