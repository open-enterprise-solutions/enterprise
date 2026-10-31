/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaAccordionItemCollapsedEvent
} from "./chunk.4MQTK254.js";
import {
  WaAccordionItemExpandedEvent
} from "./chunk.M7O5VY4E.js";
import {
  WaAccordionItemTriggerEvent
} from "./chunk.NSSMHWNA.js";
import {
  waitForEvent
} from "./chunk.F25QOBDY.js";
import {
  animate,
  parseDuration
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
  accordion_item_styles_default
} from "./chunk.W3JJ6XED.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/accordion-item/accordion-item.ts
var WaAccordionItem = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.animationGeneration = 0;
    this.localize = new LocalizeController(this);
    this.isAnimating = false;
    this.label = "";
    this.expanded = false;
    this.disabled = false;
    this.headingLevel = "3";
    this.iconPlacement = "end";
    this.appearance = "outlined";
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.body.style.height = this.expanded ? "auto" : "0";
  }
  updated() {
    this.customStates.set("animating", this.isAnimating);
  }
  handleTriggerClick() {
    if (this.disabled) return;
    this.dispatchEvent(new WaAccordionItemTriggerEvent({ item: this }));
  }
  handleTriggerKeyDown(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      this.handleTriggerClick();
    }
  }
  async handleExpandedChange() {
    this.animationGeneration++;
    const generation = this.animationGeneration;
    if (this.expanded) {
      this.isAnimating = true;
      const duration = parseDuration(getComputedStyle(this.body).getPropertyValue("--show-duration") || "200ms");
      const easing = getComputedStyle(this.body).getPropertyValue("--easing") || "ease";
      await animate(
        this.body,
        [
          { height: "0", opacity: "0" },
          { height: `${this.body.scrollHeight}px`, opacity: "1" }
        ],
        { duration, easing }
      );
      if (this.animationGeneration !== generation) return;
      this.body.style.height = "auto";
      this.isAnimating = false;
      this.dispatchEvent(new WaAccordionItemExpandedEvent());
    } else {
      this.isAnimating = true;
      const duration = parseDuration(getComputedStyle(this.body).getPropertyValue("--hide-duration") || "200ms");
      const easing = getComputedStyle(this.body).getPropertyValue("--easing") || "ease";
      await animate(
        this.body,
        [
          { height: `${this.body.scrollHeight}px`, opacity: "1" },
          { height: "0", opacity: "0" }
        ],
        { duration, easing }
      );
      if (this.animationGeneration !== generation) return;
      this.body.style.height = "0";
      this.isAnimating = false;
      this.dispatchEvent(new WaAccordionItemCollapsedEvent());
    }
  }
  /** Expands the accordion item with animation. */
  async expand() {
    if (this.expanded || this.disabled) return;
    this.expanded = true;
    return waitForEvent(this, "wa-accordion-item-expanded");
  }
  /** Collapses the accordion item with animation. */
  async collapse() {
    if (!this.expanded || this.disabled) return;
    this.expanded = false;
    return waitForEvent(this, "wa-accordion-item-collapsed");
  }
  /** Toggles the accordion item's expanded state. */
  async toggle() {
    return this.expanded ? this.collapse() : this.expand();
  }
  /** Focuses the accordion item's trigger button. */
  focus(options) {
    this.triggerButton?.focus(options);
  }
  renderHeadingWrapper(content) {
    const level = parseInt(this.headingLevel, 10);
    switch (level >= 1 && level <= 6 ? level : 3) {
      case 1:
        return x`<h1 part="heading">${content}</h1>`;
      case 2:
        return x`<h2 part="heading">${content}</h2>`;
      case 4:
        return x`<h4 part="heading">${content}</h4>`;
      case 5:
        return x`<h5 part="heading">${content}</h5>`;
      case 6:
        return x`<h6 part="heading">${content}</h6>`;
      default:
        return x`<h3 part="heading">${content}</h3>`;
    }
  }
  render() {
    const isRtl = !this.hasUpdated ? this.dir === "rtl" : this.localize.dir() === "rtl";
    const button = x`
      <button
        part="button"
        type="button"
        id="trigger"
        aria-expanded=${this.expanded ? "true" : "false"}
        aria-controls="panel"
        aria-disabled=${this.disabled ? "true" : "false"}
        tabindex=${this.disabled ? "-1" : "0"}
        @click=${this.handleTriggerClick}
        @keydown=${this.handleTriggerKeyDown}
      >
        <slot name="label" part="label">${this.label}</slot>
        <span part="icon">
          <slot name="icon">
            <wa-icon library="system" variant="solid" name=${isRtl ? "chevron-left" : "chevron-right"}></wa-icon>
          </slot>
        </span>
      </button>
    `;
    return x`
      <div part="base accordion-item">
        ${this.headingLevel === "none" ? button : this.renderHeadingWrapper(button)}
        <div
          part="panel"
          id="panel"
          class=${e2({ body: true, animating: this.isAnimating })}
          role="region"
          aria-labelledby="trigger"
        >
          <slot part="content" class="content"></slot>
        </div>
      </div>
    `;
  }
};
WaAccordionItem.css = accordion_item_styles_default;
__decorateClass([
  e(".body")
], WaAccordionItem.prototype, "body", 2);
__decorateClass([
  e('[part~="button"]')
], WaAccordionItem.prototype, "triggerButton", 2);
__decorateClass([
  r()
], WaAccordionItem.prototype, "isAnimating", 2);
__decorateClass([
  n()
], WaAccordionItem.prototype, "label", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaAccordionItem.prototype, "expanded", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaAccordionItem.prototype, "disabled", 2);
__decorateClass([
  n({ attribute: "heading-level", reflect: true })
], WaAccordionItem.prototype, "headingLevel", 2);
__decorateClass([
  n({ attribute: "icon-placement", reflect: true })
], WaAccordionItem.prototype, "iconPlacement", 2);
__decorateClass([
  n({ reflect: true })
], WaAccordionItem.prototype, "appearance", 2);
__decorateClass([
  watch("expanded", { waitUntilFirstUpdate: true })
], WaAccordionItem.prototype, "handleExpandedChange", 1);
WaAccordionItem = __decorateClass([
  t("wa-accordion-item")
], WaAccordionItem);

export {
  WaAccordionItem
};
