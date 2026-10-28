/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/view-change.ts
var WaViewChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-view-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaViewChangeEvent
};
