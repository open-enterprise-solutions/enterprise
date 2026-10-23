/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  o,
  require_react
} from "./chunk.XJOHOSCS.js";
import {
  WaTimeInput
} from "./chunk.SXR5N7XU.js";
import {
  __toESM
} from "./chunk.JHZRD2LV.js";

// src/react/time-input/index.ts
var React = __toESM(require_react(), 1);
var tagName = "wa-time-input";
var reactWrapper = o({
  tagName,
  elementClass: WaTimeInput,
  react: React,
  events: {
    onWaClear: "wa-clear",
    onWaShow: "wa-show",
    onWaAfterShow: "wa-after-show",
    onWaHide: "wa-hide",
    onWaAfterHide: "wa-after-hide",
    onWaInvalid: "wa-invalid"
  },
  displayName: "WaTimeInput"
});
var time_input_default = reactWrapper;

export {
  time_input_default
};
