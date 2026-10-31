/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-item-collapsed.ts
var WaAccordionItemCollapsedEvent = class extends Event {
  constructor() {
    super("wa-accordion-item-collapsed", { bubbles: false, cancelable: false, composed: false });
  }
};

export {
  WaAccordionItemCollapsedEvent
};
