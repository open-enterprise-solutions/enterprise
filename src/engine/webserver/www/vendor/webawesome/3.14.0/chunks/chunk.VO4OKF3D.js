/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-after-collapse.ts
var WaAccordionAfterCollapseEvent = class extends Event {
  constructor(detail) {
    super("wa-after-collapse", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

export {
  WaAccordionAfterCollapseEvent
};
