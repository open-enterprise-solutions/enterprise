/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/data-error.ts
var WaDataErrorEvent = class extends Event {
  constructor(detail) {
    super("wa-data-error", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaDataErrorEvent
};
