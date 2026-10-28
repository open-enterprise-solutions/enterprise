/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  buildTimeSegmentLayout,
  dayPeriodFromKey,
  formatDayPeriod,
  formatTimeSegmentText,
  isTimeEmpty,
  resolveHour12,
  timeSegmentRules,
  timeSegmentsToWire,
  wireToTimeSegments,
  withSecondsForStep
} from "./chunk.6VPDWW2I.js";
import {
  time_input_styles_default
} from "./chunk.X542U3B2.js";
import {
  WaShowEvent
} from "./chunk.4ZAKP7NY.js";
import {
  WaHideEvent
} from "./chunk.MQODJ75V.js";
import {
  WaAfterHideEvent
} from "./chunk.3NKIHICW.js";
import {
  WaAfterShowEvent
} from "./chunk.PX3HMKF7.js";
import {
  WaClearEvent
} from "./chunk.JTOY5KP3.js";
import {
  isTopDismissible,
  registerDismissible,
  unregisterDismissible
} from "./chunk.3H2FQERJ.js";
import {
  RequiredValidator
} from "./chunk.GWSUX3V5.js";
import {
  form_control_styles_default
} from "./chunk.HJ65SLMP.js";
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
  waitForEvent
} from "./chunk.F25QOBDY.js";
import {
  animateWithClass
} from "./chunk.L6CIKOFQ.js";
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
  i,
  o
} from "./chunk.TLFIX76K.js";
import {
  E,
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/internal/segmented-field/segmented-field-controller.ts
var DEFAULT_SEPARATORS = ["/", ".", "-", ":", ",", " "];
var SegmentedFieldController = class {
  constructor(host, config) {
    /** Per-segment digit buffers keyed by `${group}:${field}`. */
    this.buffers = /* @__PURE__ */ new Map();
    /** Which segment currently owns the roving tabindex / was most recently focused. */
    this.active = null;
    //
    // Internal handlers
    //
    this.handleFocus = (event) => {
      const el = event.currentTarget;
      const group = el.dataset.group;
      const field = el.dataset.segment;
      this.active = { group, field };
      for (const segment of this.segmentElements()) {
        segment.tabIndex = segment === el ? 0 : -1;
      }
    };
    this.handleBlur = (event) => {
      const el = event.currentTarget;
      const group = el.dataset.group;
      const field = el.dataset.segment;
      if (this.getBuffer(group, field)) this.flushBuffer(group, field);
    };
    this.handleKeyDown = (event) => {
      const el = event.currentTarget ?? event.composedPath().find((t2) => {
        return t2 instanceof HTMLElement && t2.dataset.group && t2.dataset.segment;
      }) ?? null;
      if (!el) return;
      const group = el.dataset.group;
      const field = el.dataset.segment;
      if (!group || !field) return;
      if (event.key === "ArrowUp" || event.key === "ArrowDown") {
        event.preventDefault();
        if (this.isReadonlyOrDisabled()) return;
        if (this.getBuffer(group, field)) this.flushBuffer(group, field);
        const delta = event.key === "ArrowUp" ? 1 : -1;
        const result = this.config.rules.step(group, field, delta);
        if (result) this.config.onCommit?.(group, field, result.value);
        return;
      }
      if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
        event.preventDefault();
        if (this.getBuffer(group, field)) this.flushBuffer(group, field);
        const visualLeft = event.key === "ArrowLeft";
        const logicalPrev = this.config.isRtl() ? !visualLeft : visualLeft;
        this.moveFocus(el, logicalPrev ? -1 : 1);
        return;
      }
      if (event.key === "Home") {
        event.preventDefault();
        this.segmentElements()[0]?.focus({ preventScroll: true });
        return;
      }
      if (event.key === "End") {
        event.preventDefault();
        const segments = this.segmentElements();
        segments[segments.length - 1]?.focus({ preventScroll: true });
        return;
      }
      if (event.key === "Tab") {
        if (this.getBuffer(group, field)) this.flushBuffer(group, field);
        return;
      }
      if (event.key === "Backspace" || event.key === "Delete") {
        event.preventDefault();
        if (this.isReadonlyOrDisabled()) return;
        const buffer = this.getBuffer(group, field);
        if (buffer) {
          this.setBuffer(group, field, "");
          this.config.onCommit?.(group, field, null);
        } else {
          const hadValue = this.config.rules.clear(group, field);
          if (hadValue) {
            this.config.onCommit?.(group, field, null);
          } else if (event.key === "Backspace") {
            this.moveFocus(el, -1);
          }
        }
        return;
      }
      if (/^[0-9]$/.test(event.key)) {
        event.preventDefault();
        if (this.isReadonlyOrDisabled()) return;
        const buffer = this.getBuffer(group, field);
        const result = this.config.rules.typeDigit(group, field, buffer, event.key);
        this.setBuffer(group, field, result.buffer);
        this.config.onCommit?.(group, field, result.value);
        if (result.advance) this.moveFocus(el, 1);
        return;
      }
      const seps = this.config.separatorKeys ?? DEFAULT_SEPARATORS;
      if (seps.includes(event.key)) {
        event.preventDefault();
        if (this.getBuffer(group, field)) this.flushBuffer(group, field);
        this.moveFocus(el, 1);
        return;
      }
    };
    this.host = host;
    this.config = config;
    host.addController(this);
  }
  //
  // ReactiveController lifecycle
  //
  hostConnected() {
  }
  hostDisconnected() {
    this.buffers.clear();
    this.active = null;
  }
  //
  // Public API
  //
  /** Get the buffer for a segment. Empty string when nothing is pending. */
  getBuffer(group, field) {
    return this.buffers.get(this.key(group, field)) ?? "";
  }
  /** Replace the buffer for a segment. Pass `''` to clear. */
  setBuffer(group, field, buffer) {
    const k = this.key(group, field);
    if (buffer) this.buffers.set(k, buffer);
    else this.buffers.delete(k);
  }
  /** Drop every pending buffer. Used by the host when the canonical value is replaced wholesale. */
  clearBuffers() {
    this.buffers.clear();
  }
  /** The active segment, or `null` if focus has never landed on one. */
  getActiveSegment() {
    return this.active;
  }
  /** Sets the active segment (does not move DOM focus). Used by hosts that programmatically restore focus. */
  setActiveSegment(group, field) {
    this.active = { group, field };
  }
  /** All segment elements in the host's shadow root, in DOM (logical) order. */
  segmentElements() {
    const root = this.host.shadowRoot;
    if (!root) return [];
    return Array.from(root.querySelectorAll("[data-segment][data-group]"));
  }
  /** The segment element for a given (group, field), or `null` if not in the DOM. */
  segmentElementFor(group, field) {
    const root = this.host.shadowRoot;
    if (!root) return null;
    return root.querySelector(`[data-group="${group}"][data-segment="${field}"]`);
  }
  /**
   * Returns the segment that should receive initial focus: the first empty segment (no value AND no buffer) per
   * `isEmpty`, otherwise the first segment in the layout. The host supplies the emptiness predicate since the
   * controller doesn't know the field's stored value.
   */
  findFocusableSegment(isEmpty) {
    const segments = this.segmentElements();
    if (segments.length === 0) return null;
    const firstEmpty = segments.find((el) => {
      const group = el.dataset.group;
      const field = el.dataset.segment;
      return isEmpty(group, field) && !this.getBuffer(group, field);
    });
    return firstEmpty ?? segments[0];
  }
  /** Restore focus to the most recently active segment (or the first segment if none has been active). */
  focusActiveSegment(options) {
    if (this.active) {
      const el = this.segmentElementFor(this.active.group, this.active.field);
      if (el) {
        el.focus({ preventScroll: true, ...options });
        return;
      }
    }
    this.segmentElements()[0]?.focus({ preventScroll: true, ...options });
  }
  /** Move focus to the neighbor segment in logical (DOM) order. */
  moveFocus(from, direction, options) {
    const segments = this.segmentElements();
    const idx = segments.indexOf(from);
    if (idx < 0) return;
    const next = segments[idx + direction];
    if (next) next.focus({ preventScroll: true, ...options });
  }
  /**
   * Commit a pending buffer to its committed value via `rules.commitBuffer`. Notifies the host via `onCommit`. Does
   * nothing if no buffer is pending.
   */
  flushBuffer(group, field) {
    const buffer = this.getBuffer(group, field);
    if (!buffer) return false;
    const value = this.config.rules.commitBuffer(group, field, buffer);
    this.setBuffer(group, field, "");
    this.config.onCommit?.(group, field, value);
    return true;
  }
  /**
   * Flush every pending buffer across all groups. Used when the host needs to read a final value (e.g., on form
   * submission outside of a focus change).
   */
  flushAllBuffers() {
    for (const [k, buffer] of this.buffers) {
      if (!buffer) continue;
      const [group, field] = k.split(":");
      const value = this.config.rules.commitBuffer(group, field, buffer);
      this.config.onCommit?.(group, field, value);
    }
    this.buffers.clear();
  }
  /**
   * Returns the event handlers a segment element should bind. Hosts wire these via `@keydown=${handlers.keydown}` etc.
   * The returned functions are stable references safe to pass to Lit's directive cache.
   *
   * Hosts that need host-level shortcuts (e.g. `Alt+ArrowDown` to open a popup) should call `handleKeyDown` themselves
   * after handling their own keys. The controller's `keydown` handler is safe to invoke when the host has not consumed
   * the event — it ignores keys it doesn't recognize.
   */
  eventHandlers() {
    return {
      keydown: this.handleKeyDown,
      focus: this.handleFocus,
      blur: this.handleBlur
    };
  }
  /**
   * Direct entry point for hosts that wrap the controller's keydown with their own pre-handler (e.g. for popup
   * shortcuts). Returns `true` if the controller consumed the event (called `preventDefault`), `false` otherwise.
   */
  handleKeyDownEvent(event) {
    const defaultPreventedBefore = event.defaultPrevented;
    this.handleKeyDown(event);
    return event.defaultPrevented && !defaultPreventedBefore;
  }
  //
  // Helpers
  //
  key(group, field) {
    return `${group}:${field}`;
  }
  isReadonlyOrDisabled() {
    return !!(this.config.isReadonly?.() || this.config.isDisabled?.());
  }
};

// src/styles/component/segmented-field.styles.ts
var segmented_field_styles_default = i`
  /* font: inherit lifts the UA default button font-size so children that size with em
     (e.g. the expand icon) resolve against the host size-driven font-size instead of ~13px. */
  [part~='clear-button'],
  [part~='expand-button'] {
    flex: 0 0 auto;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: transparent;
    border: none;
    cursor: pointer;
    color: var(--wa-color-neutral-on-quiet);
    font: inherit;
    padding: 0.25em;
    /* Trailing padding overhangs the content edge rather than displacing the glyph. */
    margin-inline-end: -0.25em;
    border-radius: var(--wa-border-radius-s);
    transition: color var(--wa-transition-normal);

    @media (hover: hover) {
      &:hover {
        color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));
      }
    }

    &:active {
      color: color-mix(in oklab, currentColor, var(--wa-color-mix-active));
    }
  }

  /* Fixed widths (= glyph + 2×0.25em padding) keep each glyph centered on the trailing axis
     regardless of the slotted icon's intrinsic width. */
  [part~='expand-button'] {
    inline-size: 1.75em;
    /* Leading gap that lands the clear button on <wa-select>'s clear axis. Scales with the
       form-control padding token (like select's own spacing) so it holds across themes; the
       0.125em offset accounts for the fixed button widths. */
    margin-inline-start: calc(var(--wa-form-control-padding-inline) - 0.125em);
  }

  [part~='clear-button'] {
    inline-size: 1.5em;
    margin-inline-start: var(--wa-form-control-padding-inline);
  }

  [part~='expand-button']:focus-visible {
    outline: var(--wa-focus-ring-style) var(--wa-focus-ring-width) var(--wa-color-focus);
    outline-offset: 2px;
  }

  /* font-size scales the glyph with the host size attribute; the button width handles centering.
     Color is inherited from the button so the glyph follows its state. */
  [part~='expand-icon'] {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 1.25em;
  }

  /* Start / end decoration slots. Spaced with the same --wa-form-control-padding-inline gap as
     <wa-input>/<wa-select> so slotted icons line up with the rest of the form controls, rather
     than the tighter 0.25em the pickers used before. */
  [part~='start'],
  [part~='end'] {
    display: inline-flex;
    align-items: center;
    color: var(--wa-color-neutral-on-quiet);
  }

  [part~='start']::slotted(*) {
    margin-inline-end: var(--wa-form-control-padding-inline);
  }

  [part~='end']::slotted(*) {
    margin-inline-start: var(--wa-form-control-padding-inline);
  }
`;

// src/components/time-input/time-input.ts
var uniqueId = 0;
var generateId = () => `wa-time-input-${++uniqueId}`;
var SINGLE_GROUP = "single";
var WaTimeInput = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    /** Every segment edit dispatches `input`, so a single observed `input` event marks the field as interacted with. */
    this.assumeInteractionOn = ["input"];
    this.hasSlotController = new HasSlotController(this, "hint", "label", "footer");
    this.localize = new LocalizeController(this);
    this.popupId = generateId();
    this.keyboardHelpId = `${this.popupId}-help`;
    this.pendingValue = null;
    /** When true, the next `show()` will move focus into the first popup column (set by Alt+ArrowDown). */
    this.moveFocusToColumnOnShow = false;
    /** Debounces duplicate `change` events when the value hasn't transitioned. */
    this.lastEmittedValue = "";
    this.segments = { hour: null, minute: null, second: null, dayPeriod: null };
    /**
     * Generic segmented-input plumbing. Owns per-segment digit buffers, roving tabindex, navigation keys
     * (arrows / Home / End / Tab flush / Backspace / Delete), and separator advance. Time-specific rules
     * (per-field digit semantics, wraparound stepping, layout derivation) are plugged in below.
     */
    this.segmentsController = new SegmentedFieldController(this, {
      getLayout: () => this.getLayout(),
      isRtl: () => this.isRtl,
      isReadonly: () => this.readonly,
      isDisabled: () => this.disabled,
      rules: timeSegmentRules({
        getSegments: () => this.segments,
        setSegments: (_group, next) => {
          this.segments = next;
        },
        hour12: () => this.resolvedHour12
      }),
      onCommit: () => {
        this.recomputeValue();
        this.requestUpdate();
      }
    });
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
    this.withClear = false;
    this.withNow = false;
    this.withLabel = false;
    this.withHint = false;
    this.min = "";
    this.max = "";
    this.step = 60;
    this.hourFormat = "auto";
    this.open = false;
    this.placement = "bottom-start";
    this.distance = 0;
    this.handleDocumentFocusIn = (event) => {
      const path = event.composedPath();
      if (!path.includes(this)) this.hide();
    };
    this.handleDocumentKeyDown = (event) => {
      if (event.key === "Escape" && this.open && isTopDismissible(this)) {
        event.stopPropagation();
        event.preventDefault();
        this.hide();
      }
    };
    this.handleDocumentMouseDown = (event) => {
      const path = event.composedPath();
      if (!path.includes(this)) this.hide();
    };
    //
    // Segment input handlers
    //
    // The popup opens on a pointer click into the field but NOT when focus arrives via Tab, since that would
    // interfere with tab order. Opening is also explicit via the toggle button or Alt+ArrowDown.
    this.handleSegmentFocus = (event) => {
      this.segmentsController.eventHandlers().focus(event);
    };
    this.handleSegmentBlur = (event) => {
      this.segmentsController.eventHandlers().blur(event);
    };
    this.handleInputWrapperPointerDown = (event) => {
      if (this.disabled || this.readonly || this.open) return;
      for (const node of event.composedPath()) {
        if (node === this) break;
        if (!(node instanceof Element)) continue;
        const tag = node.tagName;
        if (tag === "BUTTON" || tag === "A" || node.getAttribute("role") === "button") return;
      }
      this.show();
    };
    this.handleSegmentKeyDown = (event) => {
      const el = event.currentTarget;
      const field = el.dataset.segment;
      if (event.altKey && event.key === "ArrowDown") {
        event.preventDefault();
        this.moveFocusToColumnOnShow = true;
        if (this.open) {
          this.focusFirstColumn();
        } else {
          this.show();
        }
        return;
      }
      if (event.altKey && event.key === "ArrowUp") {
        event.preventDefault();
        this.hide();
        return;
      }
      if (event.key === "Enter") {
        event.preventDefault();
        if (this.segmentsController.getBuffer(SINGLE_GROUP, field)) {
          this.segmentsController.flushBuffer(SINGLE_GROUP, field);
          this.recomputeValue();
        }
        if (this.open) this.hide();
        return;
      }
      if (field === "dayPeriod") {
        const period = dayPeriodFromKey(event.key);
        if (period != null) {
          event.preventDefault();
          if (this.readonly) return;
          this.segments = { ...this.segments, dayPeriod: period };
          this.recomputeValue();
          this.requestUpdate();
          this.segmentsController.moveFocus(el, 1);
          return;
        }
      }
      this.segmentsController.eventHandlers().keydown(event);
    };
    //
    // Other handlers
    //
    this.handleExpandButtonClick = () => {
      if (this.open) {
        this.hide();
      } else {
        this.moveFocusToColumnOnShow = true;
        this.show();
      }
    };
    this.handleClearClick = (event) => {
      event.stopPropagation();
      if (!this._value && isTimeEmpty(this.segments)) return;
      this._value = "";
      this.valueHasChanged = true;
      this.segmentsController.clearBuffers();
      this.syncSegmentsFromCanonical();
      this.updateValidity();
      this.dispatchEvent(new WaClearEvent());
      this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      this.lastEmittedValue = "";
      this.focus();
    };
    this.handleClearMouseDown = (event) => {
      event.preventDefault();
      event.stopPropagation();
    };
    this.handleNowClick = () => {
      const now = /* @__PURE__ */ new Date();
      this.value = now;
      this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      this.lastEmittedValue = this._value;
      this.hide();
    };
    this.handleColumnItemClick = (event) => {
      const target = event.target.closest(".column-item");
      if (!target || target.getAttribute("aria-disabled") === "true") return;
      const field = target.dataset.field;
      const value = Number(target.dataset.value);
      if (Number.isNaN(value)) return;
      this.segments = { ...this.segments, [field]: value };
      this.recomputeValue();
      this.requestUpdate();
    };
    this.handleColumnKeyDown = (event) => {
      const column = event.currentTarget;
      const field = column.dataset.field;
      if (event.key === "Escape") {
        event.preventDefault();
        this.hide();
        return;
      }
      if (event.key === "Enter") {
        event.preventDefault();
        this.hide();
        return;
      }
      if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
        event.preventDefault();
        const fields = this.columnFields;
        if (fields.length < 2) return;
        const delta = event.key === "ArrowLeft" ? -1 : 1;
        const currentIdx = fields.indexOf(field);
        const nextIdx = ((currentIdx + delta) % fields.length + fields.length) % fields.length;
        const nextField = fields[nextIdx];
        const nextColumn = this.shadowRoot?.querySelector(`.column[data-field="${nextField}"]`);
        nextColumn?.focus({ preventScroll: true });
        return;
      }
      if (event.key === "ArrowUp" || event.key === "ArrowDown" || event.key === "PageUp" || event.key === "PageDown") {
        event.preventDefault();
        const delta = event.key === "ArrowUp" || event.key === "PageUp" ? -1 : 1;
        const jump = event.key === "PageUp" || event.key === "PageDown" ? 5 : 1;
        const items = this.columnItemsFor(field);
        if (items.length === 0) return;
        const currentValue = this.segments[field];
        const idx = currentValue == null ? 0 : Math.max(
          0,
          items.findIndex((it) => it.value === currentValue)
        );
        const nextIdx = ((idx + delta * jump) % items.length + items.length) % items.length;
        const nextItem = items[nextIdx];
        this.segments = { ...this.segments, [field]: nextItem.value };
        this.recomputeValue();
        this.requestUpdate();
        requestAnimationFrame(() => {
          const itemEl = column.querySelector(`[data-value="${nextItem.value}"]`);
          if (itemEl) this.keepItemInView(column, itemEl);
        });
        return;
      }
      if (event.key === "Home") {
        event.preventDefault();
        const items = this.columnItemsFor(field);
        if (items.length === 0) return;
        this.segments = { ...this.segments, [field]: items[0].value };
        this.recomputeValue();
        this.requestUpdate();
        return;
      }
      if (event.key === "End") {
        event.preventDefault();
        const items = this.columnItemsFor(field);
        if (items.length === 0) return;
        const lastItem = items[items.length - 1];
        this.segments = { ...this.segments, [field]: lastItem.value };
        this.recomputeValue();
        this.requestUpdate();
        return;
      }
    };
  }
  static get validators() {
    const validators = o ? [] : [
      RequiredValidator({
        validationElement: Object.assign(document.createElement("input"), { required: true })
      }),
      // Mirrors the hidden native `<input type="time">` so its min/max/step constraints (including
      // reversed/overnight ranges) surface as rangeUnderflow/rangeOverflow/stepMismatch validity.
      MirrorValidator()
    ];
    return [...super.validators, ...validators];
  }
  /** Localized term lookup. Falls back to the English string if a locale hasn't translated the key yet. */
  term(key, fallback) {
    return this.localize.term(key) || fallback;
  }
  get validationTarget() {
    return this.valueInput;
  }
  /**
   * The time picker's value as a wire-format string matching HTML `<input type="time">`: `HH:mm`, `HH:mm:ss`, or
   * `HH:mm:ss.sss` (always 24-hour). The setter also accepts a `Date` (extracts local h/m/s) or `null`.
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
      this.syncSegmentsFromCanonical();
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
  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeOpenListeners();
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    if (this.pendingValue != null) {
      this._value = this.pendingValue;
      this.pendingValue = null;
    } else if (!this._value && this.defaultValue) {
      this._value = this.defaultValue;
    }
    this.syncSegmentsFromCanonical();
    this.input = this.valueInput;
    this.updateValidity();
    this.lastEmittedValue = this._value;
  }
  updated(changed) {
    super.updated?.(changed);
    if (changed.has("value")) {
      this.customStates.set("blank", !this.value);
    }
    if (changed.has("disabled")) {
      this.customStates.set("disabled", this.disabled);
    }
    if (changed.has("open")) {
      this.customStates.set("open", this.open);
    }
    if (changed.has("step") || changed.has("hourFormat")) {
      this.syncSegmentsFromCanonical();
    }
    if (changed.has("min") || changed.has("max") || changed.has("step")) {
      this.updateValidity();
    }
  }
  handleDisabledChange() {
    if (this.disabled && this.open) {
      this.open = false;
    }
  }
  async handleOpenChange() {
    if (this.open && !this.disabled) {
      const showEvent = new WaShowEvent();
      this.dispatchEvent(showEvent);
      if (showEvent.defaultPrevented) {
        this.open = false;
        return;
      }
      this.addOpenListeners();
      this.popup.active = true;
      await this.updateComplete;
      await animateWithClass(this.popup.popup, "show");
      this.scrollColumnsToCurrent();
      if (this.moveFocusToColumnOnShow) {
        this.moveFocusToColumnOnShow = false;
        this.focusFirstColumn();
      }
      this.dispatchEvent(new WaAfterShowEvent());
    } else {
      const hideEvent = new WaHideEvent();
      this.dispatchEvent(hideEvent);
      if (hideEvent.defaultPrevented) {
        this.open = true;
        return;
      }
      this.removeOpenListeners();
      await animateWithClass(this.popup.popup, "hide");
      this.popup.active = false;
      this.dispatchEvent(new WaAfterHideEvent());
      const active = this.shadowRoot?.activeElement;
      if (active && this.popup?.contains(active)) {
        this.focusActiveSegment();
      }
    }
  }
  //
  // Public API
  //
  /** Sets focus on the first empty (else first) segment. */
  focus(options) {
    const target = this.segmentsController.findFocusableSegment((_g, f) => this.segments[f] == null);
    target?.focus(options);
  }
  /** Removes focus from the time picker. */
  blur() {
    const active = this.shadowRoot?.activeElement;
    active?.blur();
  }
  /** Opens the popup. */
  async show() {
    if (this.open || this.disabled) return;
    this.open = true;
    await waitForEvent(this, "wa-after-show");
  }
  /** Closes the popup. */
  async hide() {
    if (!this.open || this.disabled) return;
    this.open = false;
    await waitForEvent(this, "wa-after-hide");
  }
  /** The time as a `Date` (today + wire value), or `null` when empty. */
  get valueAsDate() {
    const value = this.value;
    if (!value) return null;
    const segments = wireToTimeSegments(value, { hour12: false, withSeconds: this.resolvedWithSeconds });
    if (segments.hour == null || segments.minute == null) return null;
    const d = /* @__PURE__ */ new Date();
    d.setHours(segments.hour, segments.minute, segments.second ?? 0, 0);
    return d;
  }
  /** Milliseconds since midnight, or `NaN` when empty. */
  get valueAsNumber() {
    const d = this.valueAsDate;
    if (!d) return Number.NaN;
    return d.getHours() * 36e5 + d.getMinutes() * 6e4 + d.getSeconds() * 1e3;
  }
  //
  // Form association
  //
  formResetCallback() {
    this._value = this.defaultValue;
    this.valueHasChanged = false;
    this.segmentsController.clearBuffers();
    this.syncSegmentsFromCanonical();
    super.formResetCallback();
    this.lastEmittedValue = this._value;
    this.requestUpdate();
  }
  formStateRestoreCallback(state) {
    if (typeof state === "string") {
      this._value = state;
      if (this.hasUpdated) {
        this.syncSegmentsFromCanonical();
      } else {
        this.pendingValue = state;
      }
      this.requestUpdate();
    }
    this.updateValidity();
  }
  //
  // Internal helpers
  //
  get resolvedLocale() {
    return this.localize.lang() || "en";
  }
  get isRtl() {
    return this.localize.dir() === "rtl";
  }
  /** Final hour12 decision: `auto` defers to the locale; explicit `'12'` / `'24'` overrides. */
  get resolvedHour12() {
    if (this.hourFormat === "12") return true;
    if (this.hourFormat === "24") return false;
    return resolveHour12(this.resolvedLocale);
  }
  /** Whether the seconds segment is shown given the current `step` value. */
  get resolvedWithSeconds() {
    return withSecondsForStep(this.step);
  }
  getLayout() {
    return buildTimeSegmentLayout(this.resolvedLocale, {
      hour12: this.resolvedHour12,
      withSeconds: this.resolvedWithSeconds
    });
  }
  normalizeIncomingValue(val) {
    if (val == null) return "";
    if (typeof val === "string") return val;
    if (val instanceof Date) {
      const hh = String(val.getHours()).padStart(2, "0");
      const mm = String(val.getMinutes()).padStart(2, "0");
      const ss = String(val.getSeconds()).padStart(2, "0");
      return this.resolvedWithSeconds ? `${hh}:${mm}:${ss}` : `${hh}:${mm}`;
    }
    return "";
  }
  /** Recompute the segment state from the canonical `_value`. Discards any in-progress digit buffers. */
  syncSegmentsFromCanonical() {
    this.segmentsController.clearBuffers();
    this.segments = wireToTimeSegments(this._value, {
      hour12: this.resolvedHour12,
      withSeconds: this.resolvedWithSeconds
    });
    this.updateHiddenInput();
  }
  updateHiddenInput() {
    if (this.valueInput) {
      this.valueInput.value = this._value;
    }
    this.setValue(this._value || null);
  }
  /**
   * Recompute the canonical value from the current segments. Fires `input` always, and `change` when the value
   * transitions, matching `<input type="time">` semantics.
   */
  recomputeValue() {
    const oldValue = this._value;
    const newValue = timeSegmentsToWire(this.segments, {
      hour12: this.resolvedHour12,
      withSeconds: this.resolvedWithSeconds
    });
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
  //
  // Popup listeners
  //
  addOpenListeners() {
    document.addEventListener("focusin", this.handleDocumentFocusIn);
    document.addEventListener("keydown", this.handleDocumentKeyDown);
    document.addEventListener("mousedown", this.handleDocumentMouseDown);
    registerDismissible(this);
  }
  removeOpenListeners() {
    document.removeEventListener("focusin", this.handleDocumentFocusIn);
    document.removeEventListener("keydown", this.handleDocumentKeyDown);
    document.removeEventListener("mousedown", this.handleDocumentMouseDown);
    unregisterDismissible(this);
  }
  //
  // Segment focus restore
  //
  focusActiveSegment() {
    const active = this.segmentsController.getActiveSegment();
    if (active) {
      const el = this.segmentsController.segmentElementFor(active.group, active.field);
      if (el) {
        el.focus({ preventScroll: true });
        return;
      }
    }
    this.segmentsController.findFocusableSegment((_g, f) => this.segments[f] == null)?.focus({ preventScroll: true });
  }
  //
  // Popup columns
  //
  /** Field list for the visible columns, based on the resolved layout. */
  get columnFields() {
    return this.getLayout().order.filter((f) => f !== void 0);
  }
  columnItemsFor(field) {
    if (field === "dayPeriod") {
      return [
        { label: this.term("am", formatDayPeriod(this.resolvedLocale, 0)), value: 0, disabled: false },
        { label: this.term("pm", formatDayPeriod(this.resolvedLocale, 1)), value: 1, disabled: false }
      ];
    }
    if (field === "hour") {
      const items2 = [];
      if (this.resolvedHour12) {
        for (let h = 1; h <= 12; h++) items2.push({ label: String(h).padStart(2, "0"), value: h, disabled: false });
      } else {
        for (let h = 0; h <= 23; h++) items2.push({ label: String(h).padStart(2, "0"), value: h, disabled: false });
      }
      return items2;
    }
    const stepSec = typeof this.step === "number" && Number.isFinite(this.step) && this.step > 0 ? this.step : 1;
    const stride = field === "minute" ? stepSec < 60 ? 1 : Math.max(1, Math.floor(stepSec / 60)) : Math.max(1, Math.floor(stepSec));
    const items = [];
    for (let v = 0; v < 60; v += stride) {
      items.push({ label: String(v).padStart(2, "0"), value: v, disabled: false });
    }
    return items;
  }
  focusFirstColumn() {
    if (!this.shadowRoot) return;
    const first = this.shadowRoot.querySelector(".column");
    first?.focus({ preventScroll: true });
  }
  scrollColumnsToCurrent() {
    if (!this.shadowRoot) return;
    for (const column of this.shadowRoot.querySelectorAll(".column")) {
      const field = column.dataset.field;
      const value = this.segments[field];
      if (value == null) continue;
      const item = column.querySelector(`[data-value="${value}"]`);
      if (item) this.keepItemInView(column, item);
    }
  }
  /**
   * Scroll `column` only as far as needed to keep `item` in view. Equivalent to `scrollIntoView({ block: 'nearest' })`
   * but scoped to the column so it can't scroll ancestors (the page, popup, etc.). Measured with
   * `getBoundingClientRect` because the items' `offsetParent` isn't the column.
   */
  keepItemInView(column, item) {
    const columnRect = column.getBoundingClientRect();
    const itemRect = item.getBoundingClientRect();
    if (itemRect.top < columnRect.top) {
      column.scrollTop += itemRect.top - columnRect.top;
    } else if (itemRect.bottom > columnRect.bottom) {
      column.scrollTop += itemRect.bottom - columnRect.bottom;
    }
  }
  //
  // Segment placeholders / a11y labels
  //
  /** Visual placeholder rendered in an empty segment. Matches the native `<input type="time">` UI. */
  placeholderFor(_field) {
    return "--";
  }
  /** Localized readable name of the field, used for the spinbutton's aria-label. */
  fieldLabelFor(field) {
    const fallback = field === "hour" ? "Hour" : field === "minute" ? "Minute" : field === "second" ? "Second" : "AM/PM";
    return this.term(field, fallback);
  }
  segmentAriaValueText(field) {
    const value = this.segments[field];
    const buffer = this.segmentsController.getBuffer(SINGLE_GROUP, field);
    if (buffer) return buffer;
    if (value == null) return this.term("empty", "Empty");
    if (field === "dayPeriod") {
      return value === 0 ? this.term("am", "AM") : this.term("pm", "PM");
    }
    return String(value);
  }
  //
  // Render
  //
  render() {
    const hasLabelSlot = this.hasUpdated ? this.hasSlotController.test("label") : this.withLabel;
    const hasHintSlot = this.hasUpdated ? this.hasSlotController.test("hint") : this.withHint;
    const hasFooterSlot = this.hasUpdated ? this.hasSlotController.test("footer") : false;
    const hasLabel = !!this.label || !!hasLabelSlot;
    const hasHint = !!this.hint || !!hasHintSlot;
    const hasValue = !!this._value;
    const layout = this.getLayout();
    const groupAriaLabel = this.label || this.term("time", "Time");
    return x`
      <div
        part="form-control"
        class=${e2({
      "form-control": true,
      "form-control-has-label": hasLabel
    })}
      >
        <label
          id="label"
          part="form-control-label label"
          class=${e2({ label: true, "has-label": hasLabel })}
          aria-hidden=${hasLabel ? "false" : "true"}
          @click=${() => this.focus()}
        >
          <slot name="label">${this.label}</slot>
        </label>

        <div part="form-control-input" class="form-control-input">
          <wa-popup
            class=${e2({ "time-input-popup": true, open: this.open })}
            placement=${this.placement}
            distance=${this.distance}
            ?active=${this.open}
            flip
            shift
          >
            <div
              part="base time-input input-wrapper"
              class="input-wrapper"
              slot="anchor"
              @pointerdown=${this.handleInputWrapperPointerDown}
            >
              <slot name="start" part="start" class="start"></slot>

              <div
                part="input"
                class="segments"
                role="group"
                aria-labelledby=${hasLabel ? "label" : E}
                aria-label=${hasLabel ? E : groupAriaLabel}
              >
                ${this.renderSegmentGroup(layout)}
              </div>

              <span id=${this.keyboardHelpId} class="visually-hidden">
                ${this.term(
      "timeInputKeyboardHelp",
      "Use arrow keys to change values; press Alt+Down Arrow to open the time picker."
    )}
              </span>

              <input
                class="value-input"
                type="time"
                tabindex="-1"
                aria-hidden="true"
                .value=${this._value}
                min=${o2(this.min || void 0)}
                max=${o2(this.max || void 0)}
                step=${o2(this.step === "any" ? "any" : String(this.step))}
                ?disabled=${this.disabled}
                ?required=${this.required}
                autocomplete=${o2(this.autocomplete || void 0)}
              />

              ${this.withClear && hasValue ? x`<button
                    part="clear-button"
                    type="button"
                    class="clear-button"
                    aria-label=${this.localize.term("clearEntry")}
                    tabindex="-1"
                    @mousedown=${this.handleClearMouseDown}
                    @click=${this.handleClearClick}
                  >
                    <slot name="clear-icon">
                      <wa-icon name="circle-xmark" library="system" variant="regular"></wa-icon>
                    </slot>
                  </button>` : E}

              <slot name="end" part="end" class="end"></slot>

              <button
                part="expand-button"
                type="button"
                class="expand-button"
                aria-label=${this.open ? this.term("closeTimeInput", "Close time picker") : this.term("chooseTime", "Choose time")}
                aria-haspopup="dialog"
                aria-expanded=${this.open ? "true" : "false"}
                aria-controls=${this.popupId}
                ?disabled=${this.disabled}
                @click=${this.handleExpandButtonClick}
              >
                <slot name="expand-icon" part="expand-icon" class="expand-icon">
                  <wa-icon library="system" name="clock"></wa-icon>
                </slot>
              </button>
            </div>

            <div
              id=${this.popupId}
              part="popup"
              class="popup-body"
              role="dialog"
              aria-modal="true"
              aria-label=${this.term("chooseTime", "Choose time")}
            >
              <div part="columns" class="columns">${this.columnFields.map((f) => this.renderColumn(f))}</div>
              ${hasFooterSlot ? x`<div class="popup-footer"><slot name="footer"></slot></div>` : this.withNow ? x`<div class="popup-footer">
                      <button part="now-button" type="button" class="now-button" @click=${this.handleNowClick}>
                        ${this.term("now", "Now")}
                      </button>
                    </div>` : E}
            </div>
          </wa-popup>
        </div>

        <slot
          id="hint"
          name="hint"
          part="hint"
          class=${e2({ "has-slotted": hasHint })}
          aria-hidden=${hasHint ? "false" : "true"}
        >
          ${this.hint}
        </slot>
      </div>
    `;
  }
  renderSegmentGroup(layout) {
    const active = this.segmentsController.getActiveSegment();
    let tabAssigned = false;
    const nodes = [];
    for (const token of layout.tokens) {
      if (token.kind === "literal") {
        nodes.push(x`<span part="segment-literal" class="segment-literal" aria-hidden="true">${token.text}</span>`);
      } else {
        const field = token.field;
        const isFirstActive = !tabAssigned && (active == null || active.field === field);
        if (isFirstActive) tabAssigned = true;
        nodes.push(this.renderSegment(field, isFirstActive));
      }
    }
    return nodes;
  }
  renderSegment(field, isTabStop) {
    const value = this.segments[field];
    const buffer = this.segmentsController.getBuffer(SINGLE_GROUP, field);
    const placeholder = this.placeholderFor(field);
    const display = formatTimeSegmentText(field, value, buffer, placeholder, this.resolvedLocale);
    const isEmptySegment = value == null && !buffer;
    const bounds = field === "hour" ? this.resolvedHour12 ? { min: 1, max: 12 } : { min: 0, max: 23 } : field === "minute" || field === "second" ? { min: 0, max: 59 } : { min: 0, max: 1 };
    const ariaValueText = this.segmentAriaValueText(field);
    return x`<span
      part="segment"
      class=${e2({ segment: true, empty: isEmptySegment, [`segment-${field}`]: true })}
      data-group=${SINGLE_GROUP}
      data-segment=${field}
      role="spinbutton"
      tabindex=${this.disabled ? -1 : isTabStop ? 0 : -1}
      aria-label=${this.fieldLabelFor(field)}
      aria-valuemin=${bounds.min}
      aria-valuemax=${bounds.max}
      aria-valuenow=${o2(value == null ? void 0 : value)}
      aria-valuetext=${ariaValueText}
      aria-readonly=${this.readonly ? "true" : "false"}
      aria-disabled=${this.disabled ? "true" : "false"}
      aria-describedby=${this.keyboardHelpId}
      inputmode=${field === "dayPeriod" ? "text" : "numeric"}
      @keydown=${this.handleSegmentKeyDown}
      @focus=${this.handleSegmentFocus}
      @blur=${this.handleSegmentBlur}
      >${display}</span
    >`;
  }
  renderColumn(field) {
    const items = this.columnItemsFor(field);
    const currentValue = this.segments[field];
    const activedescendantId = currentValue != null ? `${this.popupId}-${field}-${currentValue}` : void 0;
    return x`<div
      part="column column-${field}"
      class=${e2({ column: true, [`column-${field}`]: true })}
      data-field=${field}
      role="listbox"
      tabindex="0"
      aria-label=${this.fieldLabelFor(field)}
      aria-orientation="vertical"
      aria-activedescendant=${o2(activedescendantId)}
      @click=${this.handleColumnItemClick}
      @keydown=${this.handleColumnKeyDown}
    >
      ${items.map((item) => {
      const id = `${this.popupId}-${field}-${item.value}`;
      const selected = item.value === currentValue;
      return x`<button
          id=${id}
          part="column-item ${selected ? "column-item-selected" : ""}"
          class="column-item"
          data-field=${field}
          data-value=${item.value}
          type="button"
          role="option"
          aria-selected=${selected ? "true" : "false"}
          aria-disabled=${item.disabled ? "true" : "false"}
          tabindex="-1"
        >
          ${item.label}
        </button>`;
    })}
    </div>`;
  }
};
WaTimeInput.css = [size_styles_default, form_control_styles_default, segmented_field_styles_default, time_input_styles_default];
WaTimeInput.shadowRootOptions = {
  ...WebAwesomeFormAssociatedElement.shadowRootOptions,
  delegatesFocus: true
};
__decorateClass([
  e(".time-input-popup")
], WaTimeInput.prototype, "popup", 2);
__decorateClass([
  e(".value-input")
], WaTimeInput.prototype, "valueInput", 2);
__decorateClass([
  r()
], WaTimeInput.prototype, "segments", 2);
__decorateClass([
  n({ reflect: true })
], WaTimeInput.prototype, "name", 2);
__decorateClass([
  r()
], WaTimeInput.prototype, "value", 1);
__decorateClass([
  n({ attribute: "value", reflect: true })
], WaTimeInput.prototype, "defaultValue", 2);
__decorateClass([
  n({ type: Boolean })
], WaTimeInput.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTimeInput.prototype, "required", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTimeInput.prototype, "readonly", 2);
__decorateClass([
  n({ reflect: true })
], WaTimeInput.prototype, "size", 2);
__decorateClass([
  watch("size")
], WaTimeInput.prototype, "handleSizeChange", 1);
__decorateClass([
  n({ reflect: true })
], WaTimeInput.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTimeInput.prototype, "pill", 2);
__decorateClass([
  n()
], WaTimeInput.prototype, "label", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaTimeInput.prototype, "hint", 2);
__decorateClass([
  n()
], WaTimeInput.prototype, "autocomplete", 2);
__decorateClass([
  n({ attribute: "with-clear", type: Boolean })
], WaTimeInput.prototype, "withClear", 2);
__decorateClass([
  n({ attribute: "with-now", type: Boolean })
], WaTimeInput.prototype, "withNow", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaTimeInput.prototype, "withLabel", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaTimeInput.prototype, "withHint", 2);
__decorateClass([
  n({ reflect: true })
], WaTimeInput.prototype, "min", 2);
__decorateClass([
  n({ reflect: true })
], WaTimeInput.prototype, "max", 2);
__decorateClass([
  n({ converter: { fromAttribute: stepFromAttribute, toAttribute: stepToAttribute } })
], WaTimeInput.prototype, "step", 2);
__decorateClass([
  n({ attribute: "hour-format", reflect: true })
], WaTimeInput.prototype, "hourFormat", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTimeInput.prototype, "open", 2);
__decorateClass([
  n({ reflect: true })
], WaTimeInput.prototype, "placement", 2);
__decorateClass([
  n({ type: Number, reflect: true })
], WaTimeInput.prototype, "distance", 2);
__decorateClass([
  watch("disabled", { waitUntilFirstUpdate: true })
], WaTimeInput.prototype, "handleDisabledChange", 1);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaTimeInput.prototype, "handleOpenChange", 1);
WaTimeInput = __decorateClass([
  t("wa-time-input")
], WaTimeInput);
function stepFromAttribute(value) {
  if (value == null) return 60;
  if (value === "any") return "any";
  const n2 = Number(value);
  return Number.isFinite(n2) && n2 > 0 ? n2 : 60;
}
function stepToAttribute(value) {
  if (value === "any") return "any";
  return String(value);
}

export {
  WaTimeInput
};
