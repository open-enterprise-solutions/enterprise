/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-expand.ts
var WaAccordionExpandEvent = class extends Event {
  constructor(detail) {
    super("wa-expand", { bubbles: true, cancelable: true, composed: true });
    this.detail = detail;
  }
};

export {
  WaAccordionExpandEvent
};
