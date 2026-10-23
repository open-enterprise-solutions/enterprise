/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  checkbox_group_styles_default
} from "./chunk.6CIAVEBC.js";
import {
  form_control_styles_default
} from "./chunk.HJ65SLMP.js";
import {
  warnDeprecatedSize
} from "./chunk.RPQJAXXR.js";
import {
  HasSlotController
} from "./chunk.RWNXKUCF.js";
import {
  size_styles_default
} from "./chunk.JB5Y2AN3.js";
import {
  e
} from "./chunk.KWDPKKFO.js";
import {
  watch
} from "./chunk.PZAN6FPN.js";
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

// src/components/checkbox-group/checkbox-group.ts
var WaCheckboxGroup = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.hasSlotController = new HasSlotController(this, "hint", "label");
    this.label = "";
    this.hint = "";
    this.orientation = "vertical";
    this.required = false;
    this.withLabel = false;
    this.withHint = false;
    /**
     * Applies the group's size to each grouped checkbox/switch
     */
    this.syncCheckboxElements = () => {
      if (!this.size) return;
      for (const checkbox of this.getAllCheckboxes()) {
        checkbox.setAttribute("size", this.size);
      }
    };
  }
  handleSizeChange() {
    warnDeprecatedSize(this.localName, this.size);
  }
  updated(changedProperties) {
    if (changedProperties.has("size")) {
      this.syncCheckboxElements();
    }
  }
  /** Returns all grouped checkbox and switch elements. */
  getAllCheckboxes() {
    return [...this.querySelectorAll(":is(wa-checkbox, wa-switch)")];
  }
  render() {
    const hasLabelSlot = this.hasSlotController.test("label", "withLabel");
    const hasHintSlot = this.hasSlotController.test("hint", "withHint");
    const hasLabel = this.label ? true : !!hasLabelSlot;
    const hasHint = this.hint ? true : !!hasHintSlot;
    return x`
      <fieldset
        part="form-control"
        class=${e({
      "form-control": true,
      "checkbox-group-required": this.required,
      "form-control-has-label": hasLabel
    })}
      >
        <label
          part="form-control-label"
          id="label"
          class=${e({
      label: true,
      "has-label": hasLabel
    })}
          aria-hidden=${hasLabel ? "false" : "true"}
        >
          <slot name="label">${this.label}</slot>
        </label>

        <div part="form-control-input" role="group" aria-labelledby="label" aria-describedby="hint">
          <slot @slotchange=${this.syncCheckboxElements}></slot>
        </div>

        <slot
          id="hint"
          name="hint"
          part="hint"
          class=${e({
      "has-slotted": hasHint
    })}
          aria-hidden=${hasHint ? "false" : "true"}
          >${this.hint}</slot
        >
      </fieldset>
    `;
  }
};
WaCheckboxGroup.css = [size_styles_default, form_control_styles_default, checkbox_group_styles_default];
__decorateClass([
  n()
], WaCheckboxGroup.prototype, "label", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaCheckboxGroup.prototype, "hint", 2);
__decorateClass([
  n({ reflect: true })
], WaCheckboxGroup.prototype, "orientation", 2);
__decorateClass([
  n({ reflect: true })
], WaCheckboxGroup.prototype, "size", 2);
__decorateClass([
  watch("size")
], WaCheckboxGroup.prototype, "handleSizeChange", 1);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaCheckboxGroup.prototype, "required", 2);
__decorateClass([
  n({ type: Boolean, attribute: "with-label" })
], WaCheckboxGroup.prototype, "withLabel", 2);
__decorateClass([
  n({ type: Boolean, attribute: "with-hint" })
], WaCheckboxGroup.prototype, "withHint", 2);
WaCheckboxGroup = __decorateClass([
  t("wa-checkbox-group")
], WaCheckboxGroup);
WaCheckboxGroup.disableWarning?.("change-in-update");

export {
  WaCheckboxGroup
};
