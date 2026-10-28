/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  progress_bar_styles_default
} from "./chunk.ZSNU3QL4.js";
import {
  clamp
} from "./chunk.O6IZ4I7T.js";
import {
  o
} from "./chunk.3MSWQ3RG.js";
import {
  WebAwesomeElement,
  n,
  t
} from "./chunk.LBLI4KS5.js";
import {
  LocalizeController
} from "./chunk.DL7HG4WM.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/progress-bar/progress-bar.ts
var WaProgressBar = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.value = 0;
    this.indeterminate = false;
    this.label = "";
  }
  willUpdate(changedProperties) {
    if (this.style == null) {
      this.setStyleProperty("--percentage", `${clamp(this.value, 0, 100)}%`);
    }
    super.willUpdate(changedProperties);
  }
  updated(changedProperties) {
    if (changedProperties.has("value")) {
      requestAnimationFrame(() => {
        this.style.setProperty("--percentage", `${clamp(this.value, 0, 100)}%`);
      });
    }
    super.updated(changedProperties);
  }
  render() {
    return x`
      <div
        part="base progress-bar"
        class="progress-bar"
        role="progressbar"
        title=${o(this.title)}
        aria-label=${this.label.length > 0 ? this.label : this.localize.term("progress")}
        aria-valuemin="0"
        aria-valuemax="100"
        aria-valuenow=${this.indeterminate ? "0" : this.value}
      >
        <div part="indicator" class="indicator">
          ${!this.indeterminate ? x` <slot part="label" class="label"></slot> ` : ""}
        </div>
      </div>
    `;
  }
};
WaProgressBar.css = progress_bar_styles_default;
__decorateClass([
  n({ type: Number, reflect: true })
], WaProgressBar.prototype, "value", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaProgressBar.prototype, "indeterminate", 2);
__decorateClass([
  n()
], WaProgressBar.prototype, "label", 2);
WaProgressBar = __decorateClass([
  t("wa-progress-bar")
], WaProgressBar);

export {
  WaProgressBar
};
