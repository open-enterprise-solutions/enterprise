/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  toast_item_styles_default
} from "./chunk.PPALL6KG.js";
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
  warnDeprecatedSize
} from "./chunk.RPQJAXXR.js";
import {
  HasSlotController
} from "./chunk.RWNXKUCF.js";
import {
  size_styles_default
} from "./chunk.JB5Y2AN3.js";
import {
  variants_styles_default
} from "./chunk.K5Q2EBKV.js";
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
  WebAwesomeElement,
  e,
  n,
  r,
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

// src/components/toast-item/toast-item.ts
var WaToastItem = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.hasSlotController = new HasSlotController(this, "icon");
    this.localize = new LocalizeController(this);
    this.animationFrame = null;
    this.startTime = null;
    this.isHovering = false;
    this.isFocused = false;
    this.timeLeft = 100;
    this.variant = "neutral";
    this.size = "m";
    this.duration = 5e3;
    this.withIcon = false;
    this.tick = () => {
      if (!this.startTime) {
        return;
      }
      const elapsed = performance.now() - this.startTime;
      const progress = Math.min(elapsed / this.duration, 1);
      this.timeLeft = 100 * (1 - progress);
      if (progress < 1) {
        this.animationFrame = requestAnimationFrame(this.tick);
      } else {
        this.hide();
      }
    };
    this.handlePointerEnter = (event) => {
      if (event.pointerType === "mouse" || event.pointerType === "pen") {
        this.isHovering = true;
        this.pauseTimer();
      }
    };
    this.handlePointerLeave = () => {
      if (this.isHovering) {
        this.isHovering = false;
        this.resumeTimer();
      }
    };
    this.handleFocusIn = () => {
      this.isFocused = true;
      this.pauseTimer();
    };
    this.handleFocusOut = () => {
      this.isFocused = false;
      this.resumeTimer();
    };
  }
  handleSizeChange() {
    warnDeprecatedSize(this.localName, this.size);
  }
  connectedCallback() {
    super.connectedCallback();
    this.addEventListener("pointerenter", this.handlePointerEnter);
    this.addEventListener("pointerleave", this.handlePointerLeave);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.stopTimer();
    this.removeEventListener("pointerenter", this.handlePointerEnter);
    this.removeEventListener("pointerleave", this.handlePointerLeave);
  }
  /** @internal Starts the toast item's timer and shows it. Called by the parent toast component. */
  async startTimer() {
    const showEvent = new WaShowEvent();
    this.dispatchEvent(showEvent);
    if (showEvent.defaultPrevented) {
      return;
    }
    await this.updateComplete;
    await animateWithClass(this.toastItemElement, "show");
    this.dispatchEvent(new WaAfterShowEvent());
    if (this.duration > 0 && Number.isFinite(this.duration)) {
      this.startTime = performance.now();
      this.timeLeft = 100;
      this.tick();
    }
  }
  /** @internal Stops the toast item's timer. */
  stopTimer() {
    if (this.animationFrame !== null) {
      cancelAnimationFrame(this.animationFrame);
      this.animationFrame = null;
    }
  }
  /** Hides the toast item with animation and removes it from the DOM. */
  async hide() {
    this.stopTimer();
    const hideEvent = new WaHideEvent();
    this.dispatchEvent(hideEvent);
    if (hideEvent.defaultPrevented) {
      return;
    }
    await animateWithClass(this.toastItemElement, "hide");
    this.dispatchEvent(new WaAfterHideEvent());
    this.remove();
  }
  handleCloseClick() {
    this.hide();
  }
  pauseTimer() {
    this.stopTimer();
    this.timeLeft = 100;
  }
  resumeTimer() {
    if (!this.isHovering && !this.isFocused && this.duration > 0) {
      this.startTime = performance.now();
      this.tick();
    }
  }
  render() {
    const hasIcon = this.hasUpdated ? this.hasSlotController.test("icon") : this.withIcon;
    const hasDuration = this.duration > 0;
    return x`
      <div
        part="toast-item"
        class=${e2({
      "toast-item": true,
      "toast-item--has-icon": hasIcon,
      "toast-item--has-duration": hasDuration
    })}
      >
        <div part="accent" class="accent"></div>

        <div part="icon" class="icon">
          <slot name="icon"></slot>
        </div>

        <div part="content" class="content">
          <slot></slot>
        </div>

        <button
          part="close-button"
          class="close-button"
          type="button"
          aria-label=${this.localize.term("close")}
          @click=${this.handleCloseClick}
          @focusin=${this.handleFocusIn}
          @focusout=${this.handleFocusOut}
        >
          <wa-progress-ring
            part="progress-ring"
            exportparts="
              base:progress-ring__base,
              label:progress-ring__label,
              track:progress-ring__track,
              indicator:progress-ring__indicator
            "
            value=${this.timeLeft}
            aria-hidden="true"
          >
            <wa-icon
              part="close-icon"
              exportparts="svg:close-icon__svg"
              name="xmark"
              library="system"
              variant="solid"
            ></wa-icon>
          </wa-progress-ring>
        </button>
      </div>
    `;
  }
};
WaToastItem.css = [toast_item_styles_default, variants_styles_default, size_styles_default];
__decorateClass([
  e(".toast-item")
], WaToastItem.prototype, "toastItemElement", 2);
__decorateClass([
  r()
], WaToastItem.prototype, "timeLeft", 2);
__decorateClass([
  n({ reflect: true })
], WaToastItem.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaToastItem.prototype, "size", 2);
__decorateClass([
  watch("size")
], WaToastItem.prototype, "handleSizeChange", 1);
__decorateClass([
  n({ type: Number })
], WaToastItem.prototype, "duration", 2);
__decorateClass([
  n({ attribute: "with-icon", type: Boolean })
], WaToastItem.prototype, "withIcon", 2);
WaToastItem = __decorateClass([
  t("wa-toast-item")
], WaToastItem);

export {
  WaToastItem
};
