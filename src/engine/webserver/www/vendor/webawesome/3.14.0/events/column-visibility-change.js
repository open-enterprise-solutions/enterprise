/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/column-visibility-change.ts
var WaColumnVisibilityChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-column-visibility-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaColumnVisibilityChangeEvent
};
