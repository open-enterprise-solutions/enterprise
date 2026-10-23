/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  tooltip_styles_default
} from "./chunk.QD7TM6E5.js";
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
  WaPopup
} from "./chunk.EFKLEU6F.js";
import {
  isTopDismissible,
  registerDismissible,
  unregisterDismissible
} from "./chunk.3H2FQERJ.js";
import {
  uniqueId
} from "./chunk.O6IZ4I7T.js";
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
  WebAwesomeElement,
  e,
  n,
  r,
  t
} from "./chunk.LBLI4KS5.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/tooltip/tooltip.ts
function containsComposedNode(ancestor, node) {
  while (node) {
    if (node === ancestor) {
      return true;
    }
    if (node instanceof Element && node.assignedSlot) {
      node = node.assignedSlot;
    } else if (node.parentNode) {
      node = node.parentNode;
    } else if (node instanceof ShadowRoot) {
      node = node.host;
    } else {
      node = null;
    }
  }
  return false;
}
var WaTooltip = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    // Set when the anchor is pressed to light dismiss the tooltip. While true, hover and focus won't reopen it. Cleared
    // when the pointer fully leaves the anchor and tooltip or when the anchor blurs.
    this.dismissedByPress = false;
    this.placement = "top";
    this.disabled = false;
    this.distance = 8;
    this.open = false;
    this.skidding = 0;
    this.showDelay = 150;
    this.hideDelay = 0;
    this.trigger = "hover focus";
    this.withoutArrow = false;
    this.for = null;
    this.anchor = null;
    this.eventController = new AbortController();
    this.handleBlur = () => {
      this.dismissedByPress = false;
      if (this.hasTrigger("focus")) {
        this.hide();
      }
    };
    this.handleClick = () => {
      if (this.hasTrigger("click")) {
        if (this.open) {
          this.hide();
        } else {
          this.show();
        }
        return;
      }
      if (this.hasTrigger("manual")) {
        return;
      }
      this.lightDismiss();
    };
    this.handleFocus = () => {
      if (this.dismissedByPress) {
        return;
      }
      if (this.hasTrigger("focus")) {
        this.show();
      }
    };
    this.handleMouseDown = () => {
      if (this.hasTrigger("click") || this.hasTrigger("manual")) {
        return;
      }
      this.lightDismiss();
    };
    this.handleDocumentKeyDown = (event) => {
      if (this.hasTrigger("manual")) {
        return;
      }
      if (event.key === "Escape" && this.open && isTopDismissible(this)) {
        event.preventDefault();
        event.stopPropagation();
        this.hide();
      }
    };
    this.handleDocumentClick = (event) => {
      if (this.hasTrigger("manual")) {
        return;
      }
      if (this.anchor && event.composedPath().includes(this.anchor)) {
        return;
      }
      this.hide();
    };
    this.handleMouseOver = () => {
      if (this.dismissedByPress) {
        return;
      }
      if (this.hasTrigger("hover")) {
        clearTimeout(this.hoverTimeout);
        this.hoverTimeout = window.setTimeout(() => this.show(), this.showDelay);
      }
    };
    this.handleMouseOut = (event) => {
      const relatedTarget = event.relatedTarget;
      const movedIntoAnchor = Boolean(relatedTarget && this.anchor && containsComposedNode(this.anchor, relatedTarget));
      const movedIntoTooltip = Boolean(relatedTarget && containsComposedNode(this, relatedTarget));
      if (movedIntoAnchor || movedIntoTooltip) {
        return;
      }
      this.dismissedByPress = false;
      if (this.hasTrigger("hover")) {
        clearTimeout(this.hoverTimeout);
        this.hoverTimeout = window.setTimeout(() => {
          this.hide();
        }, this.hideDelay);
      }
    };
  }
  connectedCallback() {
    super.connectedCallback();
    const isClient = typeof document !== "undefined";
    if (isClient) {
      if (this.eventController.signal.aborted) {
        this.eventController = new AbortController();
      }
      this.addEventListener("mouseout", this.handleMouseOut);
      this.dismissedByPress = false;
      if (this.open) {
        this.open = false;
        this.updateComplete.then(() => {
          this.open = true;
        });
      }
      if (!this.id) {
        this.id = uniqueId("wa-tooltip-");
      }
      if (this.for && this.anchor) {
        this.anchor = null;
        this.handleForChange();
      } else if (this.for) {
        this.handleForChange();
      }
    }
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    document.removeEventListener("keydown", this.handleDocumentKeyDown);
    document.removeEventListener("click", this.handleDocumentClick);
    unregisterDismissible(this);
    this.eventController.abort();
    if (this.anchor) {
      this.removeFromAriaLabelledBy(this.anchor, this.id);
    }
  }
  firstUpdated(changedProperties) {
    this.body.hidden = !this.open;
    if (this.open) {
      this.popup.active = true;
      this.popup.reposition();
    }
    super.firstUpdated(changedProperties);
  }
  /** Hides the tooltip, or cancels a pending show, and keeps it hidden until re-armed. */
  lightDismiss() {
    clearTimeout(this.hoverTimeout);
    this.dismissedByPress = true;
    this.hide();
  }
  hasTrigger(triggerType) {
    const triggers = this.trigger.split(" ");
    return triggers.includes(triggerType);
  }
  /** Adds the tooltip ID to the aria-labelledby attribute */
  addToAriaLabelledBy(element, id) {
    const currentLabel = element.getAttribute("aria-labelledby") || "";
    const labels = currentLabel.split(/\s+/).filter(Boolean);
    if (!labels.includes(id)) {
      labels.push(id);
      element.setAttribute("aria-labelledby", labels.join(" "));
    }
  }
  /** Removes the tooltip ID from the aria-labelledby attribute */
  removeFromAriaLabelledBy(element, id) {
    const currentLabel = element.getAttribute("aria-labelledby") || "";
    const labels = currentLabel.split(/\s+/).filter(Boolean);
    const filteredLabels = labels.filter((label) => label !== id);
    if (filteredLabels.length > 0) {
      element.setAttribute("aria-labelledby", filteredLabels.join(" "));
    } else {
      element.removeAttribute("aria-labelledby");
    }
  }
  async handleOpenChange() {
    if (this.open) {
      if (this.disabled) {
        return;
      }
      const waShowEvent = new WaShowEvent();
      this.dispatchEvent(waShowEvent);
      if (waShowEvent.defaultPrevented) {
        this.open = false;
        return;
      }
      if (!this.hasTrigger("manual")) {
        document.addEventListener("keydown", this.handleDocumentKeyDown, { signal: this.eventController.signal });
        document.addEventListener("click", this.handleDocumentClick, { signal: this.eventController.signal });
        registerDismissible(this);
      }
      this.body.hidden = false;
      this.popup.active = true;
      await animateWithClass(this.popup.popup, "show-with-scale");
      this.popup.reposition();
      this.dispatchEvent(new WaAfterShowEvent());
    } else {
      const waHideEvent = new WaHideEvent();
      this.dispatchEvent(waHideEvent);
      if (waHideEvent.defaultPrevented) {
        this.open = true;
        return;
      }
      document.removeEventListener("keydown", this.handleDocumentKeyDown);
      document.removeEventListener("click", this.handleDocumentClick);
      unregisterDismissible(this);
      await animateWithClass(this.popup.popup, "hide-with-scale");
      this.popup.active = false;
      this.body.hidden = true;
      this.dispatchEvent(new WaAfterHideEvent());
    }
  }
  handleForChange() {
    const rootNode = this.getRootNode?.();
    if (!rootNode) {
      return;
    }
    const newAnchor = this.for ? rootNode.getElementById?.(this.for) : null;
    const oldAnchor = this.anchor;
    if (newAnchor === oldAnchor) {
      return;
    }
    this.dismissedByPress = false;
    const { signal } = this.eventController;
    if (newAnchor) {
      this.addToAriaLabelledBy(newAnchor, this.id);
      newAnchor.addEventListener("blur", this.handleBlur, { capture: true, signal });
      newAnchor.addEventListener("focus", this.handleFocus, { capture: true, signal });
      newAnchor.addEventListener("click", this.handleClick, { signal });
      newAnchor.addEventListener("mousedown", this.handleMouseDown, { signal });
      newAnchor.addEventListener("mouseover", this.handleMouseOver, { signal });
      newAnchor.addEventListener("mouseout", this.handleMouseOut, { signal });
    }
    if (oldAnchor) {
      this.removeFromAriaLabelledBy(oldAnchor, this.id);
      oldAnchor.removeEventListener("blur", this.handleBlur, { capture: true });
      oldAnchor.removeEventListener("focus", this.handleFocus, { capture: true });
      oldAnchor.removeEventListener("click", this.handleClick);
      oldAnchor.removeEventListener("mousedown", this.handleMouseDown);
      oldAnchor.removeEventListener("mouseover", this.handleMouseOver);
      oldAnchor.removeEventListener("mouseout", this.handleMouseOut);
    }
    this.anchor = newAnchor;
  }
  async handleOptionsChange() {
    if (this.hasUpdated) {
      await this.updateComplete;
      this.popup.reposition();
    }
  }
  handleDisabledChange() {
    if (this.disabled && this.open) {
      this.hide();
    }
  }
  /** Shows the tooltip. */
  async show() {
    if (this.open) {
      return void 0;
    }
    this.open = true;
    return waitForEvent(this, "wa-after-show");
  }
  /** Hides the tooltip */
  async hide() {
    if (!this.open) {
      return void 0;
    }
    this.open = false;
    return waitForEvent(this, "wa-after-hide");
  }
  render() {
    return x`
      <wa-popup
        part="base tooltip"
        exportparts="
          popup:base__popup,
          arrow:base__arrow
        "
        class=${e2({
      tooltip: true,
      "tooltip-open": this.open
    })}
        placement=${this.placement}
        distance=${this.distance}
        skidding=${this.skidding}
        flip
        shift
        ?arrow=${!this.withoutArrow}
        hover-bridge
        .anchor=${this.anchor}
      >
        <div part="body" class="body">
          <slot></slot>
        </div>
      </wa-popup>
    `;
  }
};
WaTooltip.css = tooltip_styles_default;
WaTooltip.dependencies = { "wa-popup": WaPopup };
__decorateClass([
  e("slot:not([name])")
], WaTooltip.prototype, "defaultSlot", 2);
__decorateClass([
  e(".body")
], WaTooltip.prototype, "body", 2);
__decorateClass([
  e("wa-popup")
], WaTooltip.prototype, "popup", 2);
__decorateClass([
  n()
], WaTooltip.prototype, "placement", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTooltip.prototype, "disabled", 2);
__decorateClass([
  n({ type: Number })
], WaTooltip.prototype, "distance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTooltip.prototype, "open", 2);
__decorateClass([
  n({ type: Number })
], WaTooltip.prototype, "skidding", 2);
__decorateClass([
  n({ attribute: "show-delay", type: Number })
], WaTooltip.prototype, "showDelay", 2);
__decorateClass([
  n({ attribute: "hide-delay", type: Number })
], WaTooltip.prototype, "hideDelay", 2);
__decorateClass([
  n()
], WaTooltip.prototype, "trigger", 2);
__decorateClass([
  n({ attribute: "without-arrow", type: Boolean, reflect: true })
], WaTooltip.prototype, "withoutArrow", 2);
__decorateClass([
  n()
], WaTooltip.prototype, "for", 2);
__decorateClass([
  r()
], WaTooltip.prototype, "anchor", 2);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaTooltip.prototype, "handleOpenChange", 1);
__decorateClass([
  watch("for")
], WaTooltip.prototype, "handleForChange", 1);
__decorateClass([
  watch(["distance", "placement", "skidding"])
], WaTooltip.prototype, "handleOptionsChange", 1);
__decorateClass([
  watch("disabled")
], WaTooltip.prototype, "handleDisabledChange", 1);
WaTooltip = __decorateClass([
  t("wa-tooltip")
], WaTooltip);

export {
  WaTooltip
};
