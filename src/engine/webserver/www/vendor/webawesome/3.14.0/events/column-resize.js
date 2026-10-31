/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/column-resize.ts
var WaColumnResizeEvent = class extends Event {
  constructor(detail) {
    super("wa-column-resize", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaColumnResizeEvent
};
