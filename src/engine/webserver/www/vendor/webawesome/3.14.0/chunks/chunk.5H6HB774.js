/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  o,
  require_react
} from "./chunk.XJOHOSCS.js";
import {
  WaStepper
} from "./chunk.PUN7URRY.js";
import {
  __toESM
} from "./chunk.JHZRD2LV.js";

// src/react/stepper/index.ts
var React = __toESM(require_react(), 1);
var tagName = "wa-stepper";
var reactWrapper = o({
  tagName,
  elementClass: WaStepper,
  react: React,
  events: {
    onWaBeforeStepChange: "wa-before-step-change",
    onWaStepChange: "wa-step-change"
  },
  displayName: "WaStepper"
});
var stepper_default = reactWrapper;

export {
  stepper_default
};
