/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/sort-change.ts
var WaSortChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-sort-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaSortChangeEvent
};
