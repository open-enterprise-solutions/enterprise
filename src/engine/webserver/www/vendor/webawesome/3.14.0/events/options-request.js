/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/options-request.ts
var WaOptionsRequestEvent = class extends Event {
  constructor(detail) {
    super("wa-options-request", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaOptionsRequestEvent
};
