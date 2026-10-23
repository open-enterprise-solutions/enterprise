/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/accordion-collapse.ts
var WaAccordionCollapseEvent = class extends Event {
  constructor(detail) {
    super("wa-collapse", { bubbles: true, cancelable: true, composed: true });
    this.detail = detail;
  }
};

export {
  WaAccordionCollapseEvent
};
