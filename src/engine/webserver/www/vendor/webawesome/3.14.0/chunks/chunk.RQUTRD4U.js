/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/before-step-change.ts
var WaBeforeStepChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-before-step-change", { bubbles: true, cancelable: true, composed: true });
    this.detail = detail;
  }
};

export {
  WaBeforeStepChangeEvent
};
