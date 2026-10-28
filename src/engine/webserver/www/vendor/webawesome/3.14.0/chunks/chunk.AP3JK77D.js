/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaAccordionAfterCollapseEvent
} from "./chunk.VO4OKF3D.js";
import {
  WaAccordionAfterExpandEvent
} from "./chunk.MF234WAF.js";
import {
  WaAccordionCollapseEvent
} from "./chunk.T6GGTYHD.js";
import {
  WaAccordionExpandEvent
} from "./chunk.ZZPJSMJ4.js";
import {
  accordion_styles_default
} from "./chunk.BUCKADPY.js";
import {
  watch
} from "./chunk.PZAN6FPN.js";
import {
  WebAwesomeElement,
  e,
  n,
  t
} from "./chunk.LBLI4KS5.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/accordion/accordion.ts
var WaAccordion = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.mode = "multiple";
    this.iconPlacement = "end";
    this.headingLevel = "3";
    this.appearance = "outlined";
  }
  getAllItems() {
    return this.defaultSlot.assignedElements({ flatten: true }).filter((el) => el.tagName.toLowerCase() === "wa-accordion-item");
  }
  getFocusableItems() {
    return this.getAllItems().filter((item) => !item.disabled);
  }
  ownsItem(item) {
    return item.closest("wa-accordion") === this;
  }
  handleSlotChange() {
    if (this.didSSR) {
      const promises = [];
      this.getAllItems().forEach((item) => {
        if (item.didSSR && !item.hasUpdated) {
          promises.push(item.updateComplete);
        }
      });
      if (promises.length > 0) {
        Promise.allSettled(promises).then(() => {
          this.handleSlotChange();
        });
        return;
      }
    }
    this.syncIconPlacement();
    this.syncHeadingLevel();
    this.syncAppearance();
  }
  handleKeyDown(event) {
    const items = this.getFocusableItems();
    if (!items.length) return;
    const path = event.composedPath();
    const closestItem = path.find(
      (el) => el instanceof Element && el.tagName.toLowerCase() === "wa-accordion-item"
    );
    if (!closestItem || !this.ownsItem(closestItem)) return;
    const currentIndex = items.findIndex((item) => item === closestItem);
    let nextIndex = currentIndex;
    switch (event.key) {
      case "ArrowDown":
        event.preventDefault();
        nextIndex = (currentIndex + 1) % items.length;
        break;
      case "ArrowUp":
        event.preventDefault();
        nextIndex = (currentIndex - 1 + items.length) % items.length;
        break;
      case "Home":
        event.preventDefault();
        nextIndex = 0;
        break;
      case "End":
        event.preventDefault();
        nextIndex = items.length - 1;
        break;
      default:
        return;
    }
    items[nextIndex].focus();
  }
  syncIconPlacement() {
    this.getAllItems().forEach((item) => item.iconPlacement = this.iconPlacement);
  }
  syncHeadingLevel() {
    this.getAllItems().forEach((item) => item.headingLevel = this.headingLevel);
  }
  syncAppearance() {
    this.getAllItems().forEach((item) => item.appearance = this.appearance);
  }
  async handleItemTrigger(event) {
    const { item } = event.detail;
    if (!this.ownsItem(item)) return;
    event.stopPropagation();
    if (item.disabled) return;
    if (item.expanded) {
      if (this.mode === "single") return;
      const waCollapse = new WaAccordionCollapseEvent({ item });
      this.dispatchEvent(waCollapse);
      if (waCollapse.defaultPrevented) return;
      await item.collapse();
      this.dispatchEvent(new WaAccordionAfterCollapseEvent({ item }));
    } else {
      if (this.mode === "single" || this.mode === "single-collapsible") {
        this.getAllItems().filter((i) => i !== item && i.expanded).forEach((i) => i.collapse());
      }
      const waExpand = new WaAccordionExpandEvent({ item });
      this.dispatchEvent(waExpand);
      if (waExpand.defaultPrevented) return;
      await item.expand();
      this.dispatchEvent(new WaAccordionAfterExpandEvent({ item }));
    }
  }
  /** Expands all accordion items. No-op when `mode` is `single` or `single-collapsible`. */
  expandAll() {
    if (this.mode === "single" || this.mode === "single-collapsible") return;
    this.getAllItems().filter((item) => !item.disabled && !item.expanded).forEach((item) => item.expand());
  }
  /** Collapses all accordion items. */
  collapseAll() {
    this.getAllItems().filter((item) => item.expanded).forEach((item) => item.collapse());
  }
  render() {
    return x`
      <slot
        @slotchange=${this.handleSlotChange}
        @wa-accordion-item-trigger=${this.handleItemTrigger}
        @keydown=${this.handleKeyDown}
      ></slot>
    `;
  }
};
WaAccordion.css = accordion_styles_default;
__decorateClass([
  e("slot")
], WaAccordion.prototype, "defaultSlot", 2);
__decorateClass([
  n({ reflect: true })
], WaAccordion.prototype, "mode", 2);
__decorateClass([
  n({ attribute: "icon-placement", reflect: true })
], WaAccordion.prototype, "iconPlacement", 2);
__decorateClass([
  n({ attribute: "heading-level", reflect: true })
], WaAccordion.prototype, "headingLevel", 2);
__decorateClass([
  n({ reflect: true })
], WaAccordion.prototype, "appearance", 2);
__decorateClass([
  watch("iconPlacement", { waitUntilFirstUpdate: true })
], WaAccordion.prototype, "syncIconPlacement", 1);
__decorateClass([
  watch("headingLevel", { waitUntilFirstUpdate: true })
], WaAccordion.prototype, "syncHeadingLevel", 1);
__decorateClass([
  watch("appearance", { waitUntilFirstUpdate: true })
], WaAccordion.prototype, "syncAppearance", 1);
WaAccordion = __decorateClass([
  t("wa-accordion")
], WaAccordion);

export {
  WaAccordion
};
