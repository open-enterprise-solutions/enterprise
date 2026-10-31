/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-item-trigger.ts
var WaAccordionItemTriggerEvent = class extends Event {
  constructor(detail) {
    super("wa-accordion-item-trigger", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

export {
  WaAccordionItemTriggerEvent
};
