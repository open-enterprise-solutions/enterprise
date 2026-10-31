/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-item-expanded.ts
var WaAccordionItemExpandedEvent = class extends Event {
  constructor() {
    super("wa-accordion-item-expanded", { bubbles: false, cancelable: false, composed: false });
  }
};

export {
  WaAccordionItemExpandedEvent
};
