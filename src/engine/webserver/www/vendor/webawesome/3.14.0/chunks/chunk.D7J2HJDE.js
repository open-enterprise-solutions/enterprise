/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/events/complete.ts
var WaCompleteEvent = class extends Event {
  constructor() {
    super("wa-complete", { bubbles: true, cancelable: true, composed: true });
  }
};

export {
  WaCompleteEvent
};
