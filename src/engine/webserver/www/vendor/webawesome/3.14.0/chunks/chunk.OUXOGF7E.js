/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  localeFieldOrder
} from "./chunk.TDR7XG37.js";
import {
  PartialDateValidator
} from "./chunk.ICDCAGCX.js";
import {
  EMPTY_PARTS,
  isoToParts,
  partsToIso
} from "./chunk.6TNHHCAM.js";
import {
  known_date_styles_default
} from "./chunk.PZ23AOZF.js";
import {
  RequiredValidator
} from "./chunk.GWSUX3V5.js";
import {
  form_control_styles_default
} from "./chunk.HJ65SLMP.js";
import {
  l
} from "./chunk.K5EDTD7G.js";
import {
  uniqueId
} from "./chunk.O6IZ4I7T.js";
import {
  MirrorValidator
} from "./chunk.R7QX4M6R.js";
import {
  WebAwesomeFormAssociatedElement
} from "./chunk.5VKLVAP2.js";
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
  o as o2
} from "./chunk.3MSWQ3RG.js";
import {
  e as e2
} from "./chunk.KWDPKKFO.js";
import {
  watch
} from "./chunk.PZAN6FPN.js";
import {
  e,
  n,
  r,
  t
} from "./chunk.LBLI4KS5.js";
import {
  LocalizeController
} from "./chunk.DL7HG4WM.js";
import {
  o
} from "./chunk.TLFIX76K.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/known-date/known-date.ts
var generateId = () => uniqueId("wa-known-date-");
var WaKnownDate = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    // Moving focus between the three internal fields shouldn't count as "leaving the group," so we key
    // interaction off `input` alone - matching `<wa-date-input>` and `<wa-time-input>`.
    this.assumeInteractionOn = ["input"];
    this.localize = new LocalizeController(this);
    this.hasSlotController = new HasSlotController(this, "hint", "label");
    this.groupId = generateId();
    this.hintId = `${this.groupId}-hint`;
    /** Debounces duplicate `change` events when the value hasn't transitioned. */
    this.lastEmittedValue = "";
    this.pendingValue = null;
    this.parts = { ...EMPTY_PARTS };
    this.name = "";
    this._value = "";
    this.defaultValue = this.getAttribute("value") ?? "";
    this.disabled = false;
    this.required = false;
    this.readonly = false;
    this.size = "m";
    this.appearance = "outlined";
    this.pill = false;
    this.label = "";
    this.hint = "";
    this.autocomplete = "";
    this.min = "";
    this.max = "";
    this.locale = "";
    this.withLabel = false;
    this.withHint = false;
    //
    // Field handlers
    //
    this.handleFieldInput = (event) => {
      if (this.readonly) return;
      const target = event.currentTarget;
      const field = target.dataset.field;
      const max = field === "year" ? 4 : 2;
      const sanitized = target.value.replace(/\D/g, "").slice(0, max);
      if (sanitized !== target.value) target.value = sanitized;
      this.parts = { ...this.parts, [field]: sanitized };
      this.recomputeValue();
      this.requestUpdate();
    };
  }
  static get validators() {
    const validators = o ? [] : [
      // Order matters: the first failing validator's message wins. `PartialDateValidator` comes first so a partial
      // or out-of-range entry (day 32, Feb 30) reports "Enter a valid date" as `badInput`, rather than
      // `RequiredValidator`'s "Please fill out this field" (the mirror is empty whenever the entry doesn't
      // compose).
      PartialDateValidator(),
      RequiredValidator({
        validationElement: Object.assign(document.createElement("input"), { required: true })
      }),
      // Mirrors the hidden native `<input type="date">` so its min/max constraints surface as
      // rangeUnderflow/rangeOverflow validity.
      MirrorValidator()
    ];
    return [...super.validators, ...validators];
  }
  /**
   * The committed value as an ISO `YYYY-MM-DD` string. The setter also accepts a `Date` or `null`. Reading
   * returns an empty string when the value is blank or any field is only partially filled.
   */
  get value() {
    if (this.valueHasChanged) return this._value;
    return this._value || this.defaultValue || "";
  }
  set value(val) {
    const normalized = this.normalizeIncomingValue(val);
    if (normalized === this._value) return;
    const oldValue = this._value;
    this._value = normalized;
    this.valueHasChanged = true;
    if (this.hasUpdated) {
      this.syncPartsFromCanonical();
    } else {
      this.pendingValue = this._value;
    }
    this.requestUpdate("value", oldValue);
  }
  handleSizeChange() {
    warnDeprecatedSize(this.localName, this.size);
  }
  //
  // Lifecycle
  //
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    if (this.pendingValue != null) {
      this._value = this.pendingValue;
      this.pendingValue = null;
    } else if (!this._value && this.defaultValue) {
      this._value = this.defaultValue;
    }
    this.syncPartsFromCanonical();
    this.input = this.valueInput;
    this.updateValidity();
    this.lastEmittedValue = this._value;
  }
  updated(changed) {
    super.updated?.(changed);
    if (changed.has("value")) {
      this.customStates.set("blank", !this._value);
    }
  }
  //
  // Public API
  //
  /** Focuses the first empty field, or the first field when all are filled. */
  focus(options) {
    const target = this.firstFocusableInput();
    target?.focus(options);
  }
  /** Removes focus from the known date. */
  blur() {
    const active = this.shadowRoot?.activeElement;
    active?.blur();
  }
  /** The committed value as a `Date`, or `null` when the value is empty/invalid. */
  get valueAsDate() {
    if (!this._value) return null;
    const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(this._value);
    if (!match) return null;
    return new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3]));
  }
  /**
   * Anchor native validation popups on a real visible input. The hidden mirror handles form data, but
   * anchoring a popup on `display: none` content would render it at offset (0, 0).
   */
  get validationTarget() {
    if (!this.shadowRoot) return void 0;
    const inputs = Array.from(this.shadowRoot.querySelectorAll('input[part~="field-input"]'));
    if (inputs.length === 0) return void 0;
    const offending = this.firstInvalidField();
    if (offending) {
      const el = inputs.find((i) => i.dataset.field === offending);
      if (el) return el;
    }
    return inputs[0];
  }
  //
  // Form association
  //
  formResetCallback() {
    this._value = this.defaultValue;
    this.valueHasChanged = false;
    this.syncPartsFromCanonical();
    super.formResetCallback();
    this.lastEmittedValue = this._value;
    this.requestUpdate();
  }
  formStateRestoreCallback(state) {
    if (typeof state === "string") {
      this.value = state;
    }
    this.updateValidity();
  }
  //
  // Internal helpers
  //
  get resolvedLocale() {
    return this.locale || this.localize.lang() || "en";
  }
  fieldOrder() {
    return localeFieldOrder(this.resolvedLocale);
  }
  normalizeIncomingValue(val) {
    if (val == null) return "";
    if (val instanceof Date) {
      const y = String(val.getFullYear()).padStart(4, "0");
      const m = String(val.getMonth() + 1).padStart(2, "0");
      const d = String(val.getDate()).padStart(2, "0");
      return `${y}-${m}-${d}`;
    }
    if (typeof val === "string") {
      const parts = isoToParts(val);
      return partsToIso(parts);
    }
    return "";
  }
  syncPartsFromCanonical() {
    this.parts = isoToParts(this._value);
    this.updateHiddenInput();
  }
  updateHiddenInput() {
    if (this.valueInput) {
      this.valueInput.value = this._value;
    }
    this.setValue(this._value || null);
  }
  recomputeValue() {
    const oldValue = this._value;
    const newValue = partsToIso(this.parts);
    if (newValue !== oldValue) {
      this._value = newValue;
      this.valueHasChanged = true;
      this.updateHiddenInput();
      this.updateValidity();
    }
    this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
    if (newValue !== this.lastEmittedValue) {
      this.lastEmittedValue = newValue;
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
    }
  }
  firstFocusableInput() {
    if (!this.shadowRoot) return void 0;
    const inputs = Array.from(this.shadowRoot.querySelectorAll('input[part~="field-input"]'));
    for (const field of this.fieldOrder()) {
      if (this.parts[field] === "") {
        const el = inputs.find((i) => i.dataset.field === field);
        if (el) return el;
      }
    }
    return inputs[0];
  }
  /**
   * Returns the field to fix when the value doesn't compose, in locale order: the first empty field, else the first
   * out-of-range field (year < 1, month not 1–12, day not 1–31), else the day (e.g. Feb 30). Null when the value
   * composes cleanly.
   */
  firstInvalidField() {
    if (this._value) return null;
    const order = this.fieldOrder();
    const empty = order.find((field) => this.parts[field] === "");
    if (empty) return empty;
    const inRange = {
      year: (n2) => Number.isInteger(n2) && n2 >= 1 && n2 <= 9999,
      month: (n2) => Number.isInteger(n2) && n2 >= 1 && n2 <= 12,
      day: (n2) => Number.isInteger(n2) && n2 >= 1 && n2 <= 31
    };
    const outOfRange = order.find((field) => !inRange[field](Number(this.parts[field])));
    if (outOfRange) return outOfRange;
    return "day";
  }
  autocompleteFor(field) {
    const family = this.autocomplete.trim();
    if (!family) return void 0;
    if (family === "bday") {
      if (field === "day") return "bday-day";
      if (field === "month") return "bday-month";
      return "bday-year";
    }
    if (family === "off" || family === "on") return family;
    return field === "year" ? family : void 0;
  }
  //
  // Render
  //
  render() {
    const hasLabelSlot = this.hasUpdated ? this.hasSlotController.test("label") : this.withLabel;
    const hasHintSlot = this.hasUpdated ? this.hasSlotController.test("hint") : this.withHint;
    const hasLabel = !!this.label || !!hasLabelSlot;
    const hasHint = !!this.hint || !!hasHintSlot;
    const groupAriaLabel = this.label || this.localize.term("date") || "Date";
    const userInvalid = !o && this.customStates.has("user-invalid");
    const describedBy = hasHint ? this.hintId : "";
    const fields = this.fieldOrder().map((field) => this.renderField(field, describedBy, userInvalid));
    const groupContent = x`
      <div part="base known-date form-control-input fields" class="fields">${fields}</div>

      <slot
        name="hint"
        part="hint"
        id=${this.hintId}
        class=${e2({ hint: true, "has-slotted": hasHint })}
        aria-hidden=${hasHint ? "false" : "true"}
      >
        ${this.hint}
      </slot>
    `;
    return x`
      <div
        part="form-control"
        class=${e2({
      "form-control": true,
      "form-control-has-label": hasLabel
    })}
      >
        ${hasLabel ? x`<fieldset part="fieldset" class="fieldset">
              <legend part="legend">
                <span part="form-control-label label" class="label">
                  <slot name="label">${this.label}</slot>
                </span>
              </legend>
              ${groupContent}
            </fieldset>` : x`<div part="fieldset" class="fieldset" role="group" aria-label=${groupAriaLabel}>${groupContent}</div>`}

        <input
          class="value-input"
          type="date"
          tabindex="-1"
          aria-hidden="true"
          .value=${this._value}
          min=${o2(this.min || void 0)}
          max=${o2(this.max || void 0)}
          ?disabled=${this.disabled}
          ?required=${this.required}
        />
      </div>
    `;
  }
  renderField(field, describedBy, userInvalid) {
    const fieldId = `${this.groupId}-${field}`;
    const value = this.parts[field];
    const autocompleteAttr = this.autocompleteFor(field);
    const ariaInvalid = userInvalid ? "true" : void 0;
    const fieldLabel = this.localize.term(field) || (field === "day" ? "Day" : field === "month" ? "Month" : "Year");
    return x`
      <div part="field field-${field}" class=${e2({ field: true, [`field-${field}`]: true })}>
        <input
          id=${fieldId}
          part="field-input"
          class="field-input"
          type="text"
          inputmode="numeric"
          pattern="[0-9]*"
          maxlength=${field === "year" ? 4 : 2}
          data-field=${field}
          autocomplete=${o2(autocompleteAttr)}
          aria-describedby=${o2(describedBy || void 0)}
          aria-invalid=${o2(ariaInvalid)}
          aria-required=${this.required ? "true" : "false"}
          .value=${l(value)}
          ?disabled=${this.disabled}
          ?readonly=${this.readonly}
          @input=${this.handleFieldInput}
        />
        <label part="field-label" class="field-label" for=${fieldId}>${fieldLabel}</label>
      </div>
    `;
  }
};
WaKnownDate.css = [size_styles_default, form_control_styles_default, known_date_styles_default];
WaKnownDate.shadowRootOptions = {
  ...WebAwesomeFormAssociatedElement.shadowRootOptions,
  delegatesFocus: true
};
__decorateClass([
  e(".value-input")
], WaKnownDate.prototype, "valueInput", 2);
__decorateClass([
  r()
], WaKnownDate.prototype, "parts", 2);
__decorateClass([
  n({ reflect: true })
], WaKnownDate.prototype, "name", 2);
__decorateClass([
  r()
], WaKnownDate.prototype, "value", 1);
__decorateClass([
  n({ attribute: "value", reflect: true })
], WaKnownDate.prototype, "defaultValue", 2);
__decorateClass([
  n({ type: Boolean })
], WaKnownDate.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaKnownDate.prototype, "required", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaKnownDate.prototype, "readonly", 2);
__decorateClass([
  n({ reflect: true })
], WaKnownDate.prototype, "size", 2);
__decorateClass([
  watch("size")
], WaKnownDate.prototype, "handleSizeChange", 1);
__decorateClass([
  n({ reflect: true })
], WaKnownDate.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaKnownDate.prototype, "pill", 2);
__decorateClass([
  n()
], WaKnownDate.prototype, "label", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaKnownDate.prototype, "hint", 2);
__decorateClass([
  n()
], WaKnownDate.prototype, "autocomplete", 2);
__decorateClass([
  n({ reflect: true })
], WaKnownDate.prototype, "min", 2);
__decorateClass([
  n({ reflect: true })
], WaKnownDate.prototype, "max", 2);
__decorateClass([
  n({ reflect: true })
], WaKnownDate.prototype, "locale", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaKnownDate.prototype, "withLabel", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaKnownDate.prototype, "withHint", 2);
WaKnownDate = __decorateClass([
  t("wa-known-date")
], WaKnownDate);

export {
  WaKnownDate
};
