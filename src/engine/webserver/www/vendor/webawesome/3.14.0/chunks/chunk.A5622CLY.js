/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  step_styles_default
} from "./chunk.IYKD6SDL.js";
import {
  visually_hidden_styles_default
} from "./chunk.UK2M7WPO.js";
import {
  HasSlotController
} from "./chunk.RWNXKUCF.js";
import {
  variants_styles_default
} from "./chunk.K5Q2EBKV.js";
import {
  e
} from "./chunk.KWDPKKFO.js";
import {
  watch
} from "./chunk.PZAN6FPN.js";
import {
  WebAwesomeElement,
  n,
  r,
  t
} from "./chunk.LBLI4KS5.js";
import {
  LocalizeController
} from "./chunk.DL7HG4WM.js";
import {
  E,
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/step/step.ts
var WaStep = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.hasSlotController = new HasSlotController(this, "description");
    this.name = "";
    this.completed = false;
    this.loading = false;
    this.disabled = false;
    this.variant = "brand";
    this.attention = "none";
    this.withDescription = false;
    this.position = 0;
    this.active = false;
    this.clickable = false;
    this.locked = false;
    this.role = "listitem";
  }
  handleCompletedChange() {
    this.customStates.set("completed", this.completed);
  }
  handleLoadingChange() {
    this.customStates.set("loading", this.loading);
    this.setAttribute("aria-busy", this.loading ? "true" : "false");
  }
  handleDisabledChange() {
    this.customStates.set("disabled", this.disabled);
  }
  handleActiveChange() {
    this.customStates.set("active", this.active);
    this.syncAriaCurrent();
  }
  handleLockedChange() {
    this.customStates.set("locked", this.locked);
  }
  // aria-current belongs on whichever element actually receives focus: the inner <button> when the step is
  // clickable, the host (a plain listitem) otherwise.
  syncAriaCurrent() {
    if (this.active && !this.clickable) {
      this.setAttribute("aria-current", "step");
    } else {
      this.removeAttribute("aria-current");
    }
  }
  getStatusText() {
    if (this.completed) return this.localize.term("completed");
    if (this.disabled) return this.localize.term("disabled");
    if (this.locked) return this.localize.term("locked");
    if (this.active) return "";
    return this.localize.term("notCompleted");
  }
  handleClickableChange() {
    this.customStates.set("clickable", this.clickable);
    this.syncAriaCurrent();
  }
  renderIcon() {
    if (this.loading) {
      return x`<wa-spinner part="spinner"></wa-spinner>`;
    }
    if (this.completed) {
      return x`<wa-icon name="check" library="system" variant="solid"></wa-icon>`;
    }
    return this.position > 0 ? this.localize.number(this.position) : E;
  }
  render() {
    const hasDescription = this.hasSlotController.test("description", "withDescription");
    const body = x`
      <span part="marker" class="marker">
        <slot name="icon">${this.renderIcon()}</slot>
      </span>
      <span part="content" class="content">
        <span part="label" class="label">
          <slot></slot>
        </span>
        <span part="status" class="wa-visually-hidden">${this.getStatusText()}</span>
        <span part="description" class="description" ?hidden=${!hasDescription}>
          <slot name="description"></slot>
        </span>
      </span>
    `;
    return x`
      <div part="step" class="step">
        <span
          part="connector"
          class=${e({
      "connector-start": true,
      [`wa-${this.connectorStartVariant}`]: !!this.connectorStartVariant
    })}
          ?data-completed=${!!this.connectorStartVariant}
        ></span>
        <span part="connector" class="connector-end"></span>
        ${this.clickable ? x`
              <button
                part="button"
                class="body"
                type="button"
                ?disabled=${this.disabled || this.locked}
                aria-current=${this.active ? "step" : E}
              >
                ${body}
              </button>
            ` : x`<div class="body">${body}</div>`}
      </div>
    `;
  }
};
WaStep.css = [variants_styles_default, visually_hidden_styles_default, step_styles_default];
__decorateClass([
  n({ reflect: true })
], WaStep.prototype, "name", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaStep.prototype, "completed", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaStep.prototype, "loading", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaStep.prototype, "disabled", 2);
__decorateClass([
  n({ reflect: true })
], WaStep.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaStep.prototype, "attention", 2);
__decorateClass([
  n({ type: Boolean, attribute: "with-description" })
], WaStep.prototype, "withDescription", 2);
__decorateClass([
  r()
], WaStep.prototype, "position", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaStep.prototype, "active", 2);
__decorateClass([
  r()
], WaStep.prototype, "clickable", 2);
__decorateClass([
  r()
], WaStep.prototype, "locked", 2);
__decorateClass([
  r()
], WaStep.prototype, "connectorStartVariant", 2);
__decorateClass([
  n({ reflect: true })
], WaStep.prototype, "role", 2);
__decorateClass([
  watch("completed")
], WaStep.prototype, "handleCompletedChange", 1);
__decorateClass([
  watch("loading")
], WaStep.prototype, "handleLoadingChange", 1);
__decorateClass([
  watch("disabled")
], WaStep.prototype, "handleDisabledChange", 1);
__decorateClass([
  watch("active")
], WaStep.prototype, "handleActiveChange", 1);
__decorateClass([
  watch("locked")
], WaStep.prototype, "handleLockedChange", 1);
__decorateClass([
  watch("clickable")
], WaStep.prototype, "handleClickableChange", 1);
WaStep = __decorateClass([
  t("wa-step")
], WaStep);
WaStep.disableWarning?.("change-in-update");

export {
  WaStep
};
