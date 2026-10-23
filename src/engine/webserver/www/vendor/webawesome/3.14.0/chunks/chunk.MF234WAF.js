/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-after-expand.ts
var WaAccordionAfterExpandEvent = class extends Event {
  constructor(detail) {
    super("wa-after-expand", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

export {
  WaAccordionAfterExpandEvent
};
