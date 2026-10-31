/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/row-select.ts
var WaRowSelectEvent = class extends Event {
  constructor(detail) {
    super("wa-row-select", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaRowSelectEvent
};
