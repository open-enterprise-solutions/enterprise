/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaCreateEvent
} from "./chunk.MBU7SJFC.js";
import {
  tag_input_styles_default
} from "./chunk.TSARWN6S.js";
import {
  announce
} from "./chunk.YQNBAO2Z.js";
import {
  WaClearEvent
} from "./chunk.JTOY5KP3.js";
import {
  submitOnEnter
} from "./chunk.4XYGFHPR.js";
import {
  visually_hidden_styles_default
} from "./chunk.UK2M7WPO.js";
import {
  form_control_styles_default
} from "./chunk.HJ65SLMP.js";
import {
  l
} from "./chunk.K5EDTD7G.js";
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

// src/internal/validators/tag-input-validator.ts
var TagInputValidator = () => {
  const nativeRequired = typeof document !== "undefined" && "createElement" in document ? Object.assign(document.createElement("input"), { required: true }) : void 0;
  return {
    observedAttributes: ["required", "min-tags", "max-tags"],
    checkValidity(element) {
      const validity = {
        message: "",
        isValid: true,
        invalidKeys: []
      };
      const count = element.value.length;
      if (element.required && count === 0) {
        validity.isValid = false;
        validity.invalidKeys.push("valueMissing");
        validity.message = nativeRequired?.validationMessage || "Please fill out this field.";
        return validity;
      }
      if (element.minTags != null && count > 0 && count < element.minTags) {
        validity.isValid = false;
        validity.invalidKeys.push("rangeUnderflow");
        validity.message = element.localize.term("tooFewTags", element.minTags);
        return validity;
      }
      if (element.maxTags != null && count > element.maxTags) {
        validity.isValid = false;
        validity.invalidKeys.push("rangeOverflow");
        validity.message = element.localize.term("tooManyTags", element.maxTags);
      }
      return validity;
    }
  };
};

// src/components/tag-input/tag-input.ts
var WaTagInput = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    this.assumeInteractionOn = ["blur", "input"];
    this.hasSlotController = new HasSlotController(this, "hint", "label");
    /** @internal Used by the validator to localize validation messages. */
    this.localize = new LocalizeController(this);
    this.focusedTagIndex = -1;
    this._value = null;
    this.defaultValue = this.getAttribute("value") ?? null;
    this.inputValue = "";
    this.delimiter = ",";
    this.allowDuplicates = false;
    this.withClear = false;
    this.placeholder = "";
    this.label = "";
    this.hint = "";
    this.withLabel = false;
    this.withHint = false;
    this.size = "m";
    this.appearance = "outlined";
    this.pill = false;
    this.readonly = false;
    this.required = false;
    this.spellcheck = true;
  }
  static get validators() {
    return o ? [] : [...super.validators, TagInputValidator()];
  }
  /**
   * The tags as an array of strings, submitted as one entry per tag under `name`. Set the `value` attribute to a
   * delimiter-separated string for an initial value.
   */
  get value() {
    return [...this._value ?? this.parseDelimited(this.defaultValue)];
  }
  set value(val) {
    if (val instanceof FormData) {
      val = this.name ? val.getAll(this.name) : [];
    }
    if (typeof val === "string") {
      val = this.parseDelimited(val);
    }
    const next = val ? [...val] : [];
    const old = this._value;
    if (old && old.length === next.length && old.every((tag, index) => tag === next[index])) {
      return;
    }
    this._value = next;
    this.valueHasChanged = true;
  }
  handleSizeChange() {
    warnDeprecatedSize(this.localName, this.size);
  }
  /** A regex that matches any delimiter character, or null when delimiters are turned off. */
  get delimiterRegex() {
    if (!this.delimiter) {
      return null;
    }
    return new RegExp(`[${this.delimiter.replace(/[.*+?^${}()|[\]\\-]/g, "\\$&")}]`);
  }
  get isAtMax() {
    return this.maxTags != null && this.value.length >= this.maxTags;
  }
  /** Splits a delimiter-separated string into trimmed, non-empty tags. */
  parseDelimited(str) {
    if (!str) {
      return [];
    }
    const chars = this.delimiter || ",";
    const regex = new RegExp(`[${chars.replace(/[.*+?^${}()|[\]\\-]/g, "\\$&")}]`);
    return str.split(regex).map((tag) => tag.trim()).filter(Boolean);
  }
  get tagElements() {
    return [...this.shadowRoot?.querySelectorAll('[part~="tag"]') ?? []];
  }
  /** Finds the index of the tag that contains the event target, or -1 if the target isn't inside a tag. */
  getTagIndex(target) {
    const tag = target?.closest?.('[part~="tag"]');
    return tag ? Number(tag.dataset.index) : -1;
  }
  /** Updates the text box and the inputValue property together so they never drift apart. */
  setInputText(text) {
    this.inputValue = text;
    if (this.input) {
      this.input.value = text;
    }
  }
  emitInputAndChange() {
    this.updateComplete.then(() => {
      this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
    });
  }
  /**
   * Adds a single tag from user input, subject to trimming, the tag limit, duplicate rules, and the cancelable
   * wa-create event. Returns true when the tag was added. Doesn't emit input or change.
   */
  tryAddTag(text) {
    if (this.disabled || this.readonly) {
      return false;
    }
    const tag = text.trim();
    if (!tag || this.isAtMax) {
      return false;
    }
    const tags = this.value;
    if (!this.allowDuplicates && tags.includes(tag)) {
      this.shakeTag(tags.indexOf(tag));
      announce(this.localize.term("tagAlreadyAdded", tag));
      return false;
    }
    const createEvent = new WaCreateEvent({ inputValue: tag });
    this.dispatchEvent(createEvent);
    if (createEvent.defaultPrevented) {
      return false;
    }
    this.value = [...tags, tag];
    announce(this.localize.term("tagAdded", tag));
    return true;
  }
  /**
   * Adds every string as a tag that passes the rules. Emits input and change once if any were added. Returns the
   * texts that were refused, so the caller can put them back in the text box instead of discarding them.
   */
  addTags(texts) {
    const rejected = [];
    let added = false;
    for (const text of texts) {
      if (this.tryAddTag(text)) {
        added = true;
      } else if (text.trim()) {
        rejected.push(text.trim());
      }
    }
    if (added) {
      this.hasInteracted = true;
      this.emitInputAndChange();
    }
    return rejected;
  }
  /** Removes the tag at the given index in response to user input. Returns true when a tag was removed. */
  removeTagAt(index) {
    const tags = this.value;
    if (this.disabled || this.readonly || index < 0 || index >= tags.length) {
      return false;
    }
    const [removed] = tags.splice(index, 1);
    this.hasInteracted = true;
    this.value = tags;
    announce(this.localize.term("tagRemoved", removed));
    this.emitInputAndChange();
    return true;
  }
  /** Briefly shakes an existing tag to show why a duplicate was rejected. */
  shakeTag(index) {
    const tag = this.tagElements[index];
    if (!tag || typeof tag.animate !== "function") {
      return;
    }
    if (typeof matchMedia === "function" && matchMedia("(prefers-reduced-motion: reduce)").matches) {
      return;
    }
    tag.animate(
      [
        { translate: "0" },
        { translate: "-0.2em" },
        { translate: "0.2em" },
        { translate: "-0.2em" },
        { translate: "0.2em" },
        { translate: "0" }
      ],
      { duration: 300, easing: "ease-in-out" }
    );
  }
  /** Moves roving focus to the tag at the given index after the next render. */
  async focusTag(index) {
    this.focusedTagIndex = index;
    await this.updateComplete;
    this.tagElements[index]?.focus();
  }
  handleInput(event) {
    const text = this.input.value;
    const regex = this.delimiterRegex;
    if (regex && !event.isComposing && regex.test(text)) {
      const segments = text.split(regex);
      const remainder = segments.pop() ?? "";
      const rejected = this.addTags(segments);
      this.setInputText([...rejected, remainder].filter(Boolean).join(this.delimiter[0]));
      return;
    }
    this.inputValue = text;
  }
  handlePaste(event) {
    const regex = this.delimiterRegex;
    if (this.disabled || this.readonly || !regex) {
      return;
    }
    const pasted = event.clipboardData?.getData("text/plain") ?? "";
    if (!regex.test(pasted)) {
      return;
    }
    event.preventDefault();
    const start = this.input.selectionStart ?? this.input.value.length;
    const end = this.input.selectionEnd ?? start;
    const combined = this.input.value.slice(0, start) + pasted + this.input.value.slice(end);
    const rejected = this.addTags(combined.split(regex));
    this.setInputText(rejected.join(this.delimiter[0]));
  }
  handleKeyDown(event) {
    if (event.isComposing) {
      return;
    }
    const text = this.input.value;
    const hasModifier = event.metaKey || event.ctrlKey || event.shiftKey || event.altKey;
    const isRtl = this.localize.dir() === "rtl";
    const previousKey = isRtl ? "ArrowRight" : "ArrowLeft";
    if (event.key === "Enter" && !hasModifier) {
      if (text) {
        event.preventDefault();
        if (this.addTags([text]).length === 0) {
          this.setInputText("");
        }
      } else {
        submitOnEnter(event, this);
      }
      return;
    }
    if (this.disabled || this.readonly) {
      if (event.key === "Backspace") {
        event.preventDefault();
      }
      return;
    }
    if (event.key === "Backspace" && text === "" && this.value.length > 0) {
      event.preventDefault();
      this.removeTagAt(this.value.length - 1);
      return;
    }
    if (event.key === previousKey && !hasModifier && text === "" && this.input.selectionStart === 0 && this.value.length > 0) {
      event.preventDefault();
      this.focusTag(this.value.length - 1);
      return;
    }
    if (event.key === "Escape" && text) {
      event.preventDefault();
      this.setInputText("");
      this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
    }
  }
  handleInputFocus() {
    this.focusedTagIndex = -1;
  }
  handleInputBlur() {
    if (this.input.value && !this.readonly && !this.disabled) {
      this.addTags([this.input.value]);
      this.setInputText("");
    }
  }
  handleTagKeyDown(event) {
    const index = this.getTagIndex(event.target);
    if (event.key === "Backspace" || event.key === "Delete") {
      event.preventDefault();
    }
    if (this.disabled || this.readonly || index < 0) {
      return;
    }
    const count = this.value.length;
    const hasModifier = event.metaKey || event.ctrlKey || event.altKey;
    const isRtl = this.localize.dir() === "rtl";
    const previousKey = isRtl ? "ArrowRight" : "ArrowLeft";
    const nextKey = isRtl ? "ArrowLeft" : "ArrowRight";
    switch (event.key) {
      case previousKey:
        event.preventDefault();
        this.focusTag(Math.max(0, index - 1));
        break;
      case nextKey:
        event.preventDefault();
        if (index < count - 1) {
          this.focusTag(index + 1);
        } else {
          this.input.focus();
        }
        break;
      case "Home":
        event.preventDefault();
        this.focusTag(0);
        break;
      case "End":
        event.preventDefault();
        this.input.focus();
        break;
      case "Backspace":
        event.preventDefault();
        this.removeTagAt(index);
        if (index > 0) {
          this.focusTag(index - 1);
        } else {
          this.input.focus();
        }
        break;
      case "Delete":
        event.preventDefault();
        this.removeTagAt(index);
        if (index < count - 1) {
          this.focusTag(index);
        } else if (index > 0) {
          this.focusTag(index - 1);
        } else {
          this.input.focus();
        }
        break;
      case "Escape":
        this.input.focus();
        break;
      default:
        if (event.key.length === 1 && !hasModifier) {
          this.input.focus();
        }
    }
  }
  handleTagClick(event) {
    const index = this.getTagIndex(event.target);
    if (this.disabled || index < 0) {
      return;
    }
    const isButton = event.composedPath().some((el) => el instanceof Element && el.localName === "wa-button");
    if (!isButton) {
      this.focusTag(index);
    }
  }
  handleTagFocusIn(event) {
    this.focusedTagIndex = this.getTagIndex(event.target);
  }
  handleTagFocusOut() {
    this.focusedTagIndex = -1;
  }
  handleTagRemove(event) {
    event.stopPropagation();
    if (this.removeTagAt(this.getTagIndex(event.target))) {
      this.input.focus();
    }
  }
  handleWrapperMouseDown(event) {
    const target = event.target;
    if (target === this.input || target.closest?.('[slot="start"], [slot="end"]')) {
      return;
    }
    event.preventDefault();
  }
  handleWrapperClick(event) {
    const target = event.target;
    if (this.disabled || target === this.input) {
      return;
    }
    if (target.closest?.('[slot="start"], [slot="end"], [part~="tag"], [part~="clear-button"]')) {
      return;
    }
    this.input.focus();
  }
  handleClearMouseDown(event) {
    event.preventDefault();
    event.stopPropagation();
  }
  handleClearClick(event) {
    event.stopPropagation();
    if (this.value.length > 0) {
      this.hasInteracted = true;
      this.value = [];
      this.setInputText("");
      announce(this.localize.term("allTagsRemoved"));
      this.updateComplete.then(() => {
        this.dispatchEvent(new WaClearEvent());
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      });
    }
    this.input.focus();
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    this.customStates.set("blank", this.value.length === 0);
    this.customStates.set("readonly", this.readonly);
    if (changedProperties.has("value") || changedProperties.has("defaultValue") || changedProperties.has("required") || changedProperties.has("minTags") || changedProperties.has("maxTags")) {
      this.updateValidity();
    }
  }
  formResetCallback() {
    this._value = null;
    this.setInputText("");
    super.formResetCallback();
    this.requestUpdate("value");
  }
  /** Sets focus on the text box. */
  focus(options) {
    this.input.focus(options);
  }
  /** Removes focus from the text box. */
  blur() {
    this.input.blur();
  }
  render() {
    const hasLabelSlot = this.hasSlotController.test("label", "withLabel");
    const hasHintSlot = this.hasSlotController.test("hint", "withHint");
    const hasLabel = this.label ? true : !!hasLabelSlot;
    const hasHint = this.hint ? true : !!hasHintSlot;
    const tags = this.value;
    const atMax = this.isAtMax;
    const isClearVisible = (
      // prevents hydration mismatch errors
      (!this.didSSR || this.hasUpdated) && this.withClear && !this.disabled && !this.readonly && tags.length > 0
    );
    return x`
      <label
        id="label"
        part="form-control-label"
        class=${e2({
      label: true,
      "has-label": hasLabel
    })}
        for="input"
        aria-hidden=${hasLabel ? "false" : "true"}
      >
        <slot name="label">${this.label}</slot>
      </label>

      <div
        part="tag-input"
        class=${e2({
      "tag-input": true,
      "has-tags": tags.length > 0
    })}
        @mousedown=${this.handleWrapperMouseDown}
        @click=${this.handleWrapperClick}
      >
        <slot name="start" part="start" class="start"></slot>

        ${tags.length > 0 ? x`
              ${this.readonly ? "" : x`
                    <span id="tag-help" class="wa-visually-hidden-force">
                      ${this.localize.term("tagInputKeyboardHelp")}
                    </span>
                  `}
              <div
                part="tags"
                class="tags"
                role="list"
                aria-labelledby="label"
                @keydown=${this.handleTagKeyDown}
                @click=${this.handleTagClick}
                @focusin=${this.handleTagFocusIn}
                @focusout=${this.handleTagFocusOut}
                @wa-remove=${this.handleTagRemove}
              >
                ${tags.map(
      (tag, index) => x`
                    <wa-tag
                      part="tag"
                      class=${e2({
        tag: true,
        "tag--focused": this.focusedTagIndex === index
      })}
                      exportparts="
                        content:tag__content,
                        remove-button:tag__remove-button,
                        remove-button__base:tag__remove-button__base
                      "
                      role="listitem"
                      tabindex="-1"
                      aria-describedby=${o2(this.readonly ? void 0 : "tag-help")}
                      size=${this.size}
                      ?pill=${this.pill}
                      ?with-remove=${!this.readonly}
                      data-index=${index}
                      >${tag}</wa-tag
                    >
                  `
    )}
              </div>
            ` : ""}

        <input
          part="input"
          id="input"
          class="control"
          type="text"
          .value=${l(this.inputValue)}
          ?disabled=${this.disabled}
          ?readonly=${this.readonly || atMax}
          placeholder=${o2(atMax ? void 0 : this.placeholder || void 0)}
          autocapitalize=${o2(this.autocapitalize)}
          autocomplete=${this.autocomplete ?? "off"}
          autocorrect=${this.autocorrect ? "on" : "off"}
          spellcheck=${this.spellcheck}
          enterkeyhint=${o2(this.enterkeyhint)}
          inputmode=${o2(this.inputmode)}
          aria-describedby="hint"
          aria-invalid=${this.validity.valid ? "false" : "true"}
          @input=${this.handleInput}
          @paste=${this.handlePaste}
          @keydown=${this.handleKeyDown}
          @focus=${this.handleInputFocus}
          @blur=${this.handleInputBlur}
        />

        ${isClearVisible ? x`
              <button
                part="clear-button"
                class="clear"
                type="button"
                tabindex="-1"
                aria-label=${this.localize.term("clearEntry")}
                @mousedown=${this.handleClearMouseDown}
                @click=${this.handleClearClick}
              >
                <slot name="clear-icon">
                  <wa-icon name="circle-xmark" library="system" variant="regular"></wa-icon>
                </slot>
              </button>
            ` : ""}

        <slot name="end" part="end" class="end"></slot>
      </div>

      <slot
        id="hint"
        part="hint"
        name="hint"
        class=${e2({
      "has-slotted": hasHint
    })}
        aria-hidden=${hasHint ? "false" : "true"}
        >${this.hint}</slot
      >
    `;
  }
};
WaTagInput.css = [size_styles_default, form_control_styles_default, visually_hidden_styles_default, tag_input_styles_default];
__decorateClass([
  e(".control")
], WaTagInput.prototype, "input", 2);
__decorateClass([
  r()
], WaTagInput.prototype, "focusedTagIndex", 2);
__decorateClass([
  n({ attribute: false })
], WaTagInput.prototype, "value", 1);
__decorateClass([
  n({ attribute: "value", reflect: true })
], WaTagInput.prototype, "defaultValue", 2);
__decorateClass([
  n({ attribute: false })
], WaTagInput.prototype, "inputValue", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "delimiter", 2);
__decorateClass([
  n({ attribute: "max-tags", type: Number })
], WaTagInput.prototype, "maxTags", 2);
__decorateClass([
  n({ attribute: "min-tags", type: Number })
], WaTagInput.prototype, "minTags", 2);
__decorateClass([
  n({ attribute: "allow-duplicates", type: Boolean, reflect: true })
], WaTagInput.prototype, "allowDuplicates", 2);
__decorateClass([
  n({ attribute: "with-clear", type: Boolean })
], WaTagInput.prototype, "withClear", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "placeholder", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "label", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaTagInput.prototype, "hint", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaTagInput.prototype, "withLabel", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaTagInput.prototype, "withHint", 2);
__decorateClass([
  n({ reflect: true })
], WaTagInput.prototype, "size", 2);
__decorateClass([
  watch("size")
], WaTagInput.prototype, "handleSizeChange", 1);
__decorateClass([
  n({ reflect: true })
], WaTagInput.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTagInput.prototype, "pill", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTagInput.prototype, "readonly", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTagInput.prototype, "required", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "autocapitalize", 2);
__decorateClass([
  n({
    type: Boolean,
    converter: {
      fromAttribute: (value) => !value || value === "off" ? false : true,
      toAttribute: (value) => value ? "on" : "off"
    }
  })
], WaTagInput.prototype, "autocorrect", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "autocomplete", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "enterkeyhint", 2);
__decorateClass([
  n({
    type: Boolean,
    converter: {
      // Allow "true|false" attribute values but keep the property boolean
      fromAttribute: (value) => !value || value === "false" ? false : true,
      toAttribute: (value) => value ? "true" : "false"
    }
  })
], WaTagInput.prototype, "spellcheck", 2);
__decorateClass([
  n()
], WaTagInput.prototype, "inputmode", 2);
WaTagInput = __decorateClass([
  t("wa-tag-input")
], WaTagInput);
WaTagInput.disableWarning?.("change-in-update");

export {
  WaTagInput
};
