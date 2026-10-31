/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/page-change.ts
var WaPageChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-page-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

export {
  WaPageChangeEvent
};
