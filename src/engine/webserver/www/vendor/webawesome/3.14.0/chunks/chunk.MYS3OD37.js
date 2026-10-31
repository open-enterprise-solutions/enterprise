/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/step-change.ts
var WaStepChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-step-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

export {
  WaStepChangeEvent
};
