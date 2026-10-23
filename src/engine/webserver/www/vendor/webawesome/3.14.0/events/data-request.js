/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/data-request.ts
var WaDataRequestEvent = class extends Event {
  constructor(detail) {
    super("wa-data-request", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaDataRequestEvent
};
