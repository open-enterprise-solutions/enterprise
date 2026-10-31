/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  variants_styles_default
} from "./chunk.K5Q2EBKV.js";
import {
  badge_styles_default
} from "./chunk.2NV6QW6C.js";
import {
  WebAwesomeElement,
  n,
  t
} from "./chunk.LBLI4KS5.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/badge/badge.ts
var WaBadge = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.variant = "brand";
    this.appearance = "accent";
    this.pill = false;
    this.attention = "none";
  }
  render() {
    return x`
      <span part="start">
        <slot name="start"></slot>
      </span>

      <span part="base badge" role="status">
        <slot></slot>
      </span>

      <span part="end">
        <slot name="end"></slot>
      </span>
    `;
  }
};
WaBadge.css = [variants_styles_default, badge_styles_default];
__decorateClass([
  n({ reflect: true })
], WaBadge.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaBadge.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaBadge.prototype, "pill", 2);
__decorateClass([
  n({ reflect: true })
], WaBadge.prototype, "attention", 2);
WaBadge = __decorateClass([
  t("wa-badge")
], WaBadge);

export {
  WaBadge
};
