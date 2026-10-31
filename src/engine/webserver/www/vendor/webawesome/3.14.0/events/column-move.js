/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/column-move.ts
var WaColumnMoveEvent = class extends Event {
  constructor(detail) {
    super("wa-column-move", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};
export {
  WaColumnMoveEvent
};
