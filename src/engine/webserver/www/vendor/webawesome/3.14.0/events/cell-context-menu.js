/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/events/cell-context-menu.ts
var WaCellContextmenuEvent = class extends Event {
  constructor(detail) {
    super("wa-cell-contextmenu", { bubbles: true, cancelable: true, composed: true });
    this.detail = detail;
  }
};
export {
  WaCellContextmenuEvent
};
