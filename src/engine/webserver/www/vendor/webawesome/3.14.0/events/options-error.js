/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/options-error.ts
var WaOptionsErrorEvent = class extends Event {
  constructor(detail) {
    super("wa-options-error", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaOptionsErrorEvent
};
