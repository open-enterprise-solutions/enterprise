/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/random-content-change.ts
var WaContentChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-content-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

export {
  WaContentChangeEvent
};
