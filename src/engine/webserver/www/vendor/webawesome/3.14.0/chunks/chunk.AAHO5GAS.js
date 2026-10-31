/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  o,
  require_react
} from "./chunk.XJOHOSCS.js";
import {
  WaAccordion
} from "./chunk.AP3JK77D.js";
import {
  __toESM
} from "./chunk.JHZRD2LV.js";

// src/react/accordion/index.ts
var React = __toESM(require_react(), 1);
var tagName = "wa-accordion";
var reactWrapper = o({
  tagName,
  elementClass: WaAccordion,
  react: React,
  events: {
    onWaExpand: "wa-expand",
    onWaAfterExpand: "wa-after-expand",
    onWaCollapse: "wa-collapse",
    onWaAfterCollapse: "wa-after-collapse"
  },
  displayName: "WaAccordion"
});
var accordion_default = reactWrapper;

export {
  accordion_default
};
