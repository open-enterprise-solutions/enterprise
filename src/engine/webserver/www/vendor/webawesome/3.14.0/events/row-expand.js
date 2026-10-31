/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/row-expand.ts
var WaRowExpandEvent = class extends Event {
  constructor(detail) {
    super("wa-row-expand", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaRowExpandEvent
};
