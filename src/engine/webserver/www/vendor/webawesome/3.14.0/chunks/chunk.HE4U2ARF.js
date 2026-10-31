/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaContentChangeEvent
} from "./chunk.BZMNT3VX.js";
import {
  random_content_styles_default
} from "./chunk.2YEETXBR.js";
import {
  visually_hidden_styles_default
} from "./chunk.UK2M7WPO.js";
import {
  AutoplayController
} from "./chunk.6SNQOYNK.js";
import {
  prefersReducedMotion
} from "./chunk.L6CIKOFQ.js";
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
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/random-content/random-content.ts
if (typeof document !== "undefined") {
  const sheet = new CSSStyleSheet();
  sheet.replaceSync(`
    @keyframes wa-rc-fade {
      from { opacity: 0; }
      to { opacity: 1; }
    }
    @keyframes wa-rc-fade-up {
      from { opacity: 0; transform: translateY(var(--animation-translate, 0.5em)); }
      to { opacity: 1; transform: translateY(0); }
    }
    @keyframes wa-rc-fade-down {
      from { opacity: 0; transform: translateY(calc(-1 * var(--animation-translate, 0.5em))); }
      to { opacity: 1; transform: translateY(0); }
    }
    @keyframes wa-rc-fade-left {
      from { opacity: 0; transform: translateX(var(--animation-translate, 0.5em)); }
      to { opacity: 1; transform: translateX(0); }
    }
    @keyframes wa-rc-fade-right {
      from { opacity: 0; transform: translateX(calc(-1 * var(--animation-translate, 0.5em))); }
      to { opacity: 1; transform: translateX(0); }
    }
  `);
  document.adoptedStyleSheets = [...document.adoptedStyleSheets, sheet];
}
var WaRandomContent = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.sequenceCursor = 0;
    this.uniqueQueue = [];
    this.currentSelection = /* @__PURE__ */ new Set();
    this.isInitialSelection = true;
    this.autoplayController = new AutoplayController(this, () => this.randomize());
    // Tracks the pending "clear animation attribute" listener per child so rapid re-animation
    // (e.g. fast autoplay) doesn't stack listeners — `cancel()` fires `animationcancel`, not the
    // `animationend` the listener waits for, so it wouldn't otherwise be removed.
    this.animationCleanups = /* @__PURE__ */ new WeakMap();
    this.liveAnnouncement = "";
    this.items = 1;
    this.mode = "unique";
    this.autoplay = false;
    this.autoplayInterval = 3e3;
    this.animation = "none";
  }
  connectedCallback() {
    super.connectedCallback();
    if (this.hasUpdated) this.syncAutoplay();
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.syncAutoplay();
  }
  handleAutoplayChange() {
    this.syncAutoplay();
  }
  handleModeChange() {
    this.sequenceCursor = 0;
    this.uniqueQueue = [];
    this.currentSelection.clear();
    this.randomize();
  }
  handleItemsChange() {
    this.uniqueQueue = [];
    this.randomize();
  }
  /** Selects a new set of children using the current mode. Returns the elements now shown. */
  randomize() {
    const children = this.assignedChildren();
    if (!children.length) return [];
    const count = Math.min(Math.max(1, this.items), children.length);
    let selected;
    if (this.mode === "sequence") {
      selected = [];
      Array.from({ length: count }).forEach((_, i) => {
        selected.push(children[(this.sequenceCursor + i) % children.length]);
      });
      this.sequenceCursor = (this.sequenceCursor + count) % children.length;
    } else if (this.mode === "unique") {
      if (this.uniqueQueue.length < count) {
        const queued = new Set(this.uniqueQueue);
        const rest = children.filter((c) => !this.currentSelection.has(c) && !queued.has(c));
        const current = children.filter((c) => this.currentSelection.has(c) && !queued.has(c));
        this.uniqueQueue.push(...this.sample(rest, rest.length), ...this.sample(current, current.length));
        if (this.uniqueQueue.length < count) {
          this.uniqueQueue = this.sample([...children], children.length);
        }
      }
      selected = this.uniqueQueue.splice(0, count);
      this.currentSelection = new Set(selected);
    } else {
      const pool = children.filter((c) => !this.currentSelection.has(c));
      selected = this.sample(pool.length >= count ? pool : children, count);
      this.currentSelection = new Set(selected);
    }
    const firstShown = selected[0];
    const lastShown = selected[selected.length - 1];
    children.forEach((el) => {
      const htmlEl = el;
      const isSelected = selected.includes(el);
      delete htmlEl.dataset["waAnimation"];
      htmlEl.style.display = "";
      htmlEl.hidden = !isSelected;
      htmlEl.style.marginBlockStart = isSelected && el === firstShown ? "0" : "";
      htmlEl.style.marginBlockEnd = isSelected && el === lastShown ? "0" : "";
    });
    if (this.animation !== "none" && !prefersReducedMotion()) {
      selected.forEach((el) => {
        const htmlEl = el;
        if (this.animation !== "fade" && getComputedStyle(el).display === "inline") {
          htmlEl.style.display = "inline-block";
        }
        el.getAnimations().forEach((a) => a.cancel());
        htmlEl.dataset["waAnimation"] = this.animation;
        this.animationCleanups.get(el)?.abort();
        const cleanup = new AbortController();
        this.animationCleanups.set(el, cleanup);
        htmlEl.addEventListener("animationend", () => delete htmlEl.dataset["waAnimation"], {
          once: true,
          signal: cleanup.signal
        });
      });
    }
    if (this.isInitialSelection) {
      this.isInitialSelection = false;
    } else {
      this.liveAnnouncement = selected.map((el) => el.textContent?.trim()).filter(Boolean).join(", ");
    }
    this.dispatchEvent(new WaContentChangeEvent({ items: selected }));
    return selected;
  }
  syncAutoplay() {
    this.autoplayController.stop();
    if (this.autoplay && this.autoplayInterval > 0) {
      this.autoplayController.start(this.autoplayInterval);
    }
  }
  assignedChildren() {
    const slot = this.shadowRoot?.querySelector("slot");
    return slot?.assignedElements() ?? [];
  }
  // Fisher-Yates partial shuffle — picks `count` unique items from `pool`.
  sample(pool, count) {
    const arr = [...pool];
    Array.from({ length: count }).forEach((_, i) => {
      const j = i + Math.floor(Math.random() * (arr.length - i));
      [arr[i], arr[j]] = [arr[j], arr[i]];
    });
    return arr.slice(0, count);
  }
  handleSlotChange() {
    this.randomize();
  }
  render() {
    return x`
      <slot @slotchange=${this.handleSlotChange}></slot>
      <div class="wa-visually-hidden" role="status" aria-live="polite" aria-atomic="true">${this.liveAnnouncement}</div>
    `;
  }
};
WaRandomContent.css = [random_content_styles_default, visually_hidden_styles_default];
__decorateClass([
  r()
], WaRandomContent.prototype, "liveAnnouncement", 2);
__decorateClass([
  n({ type: Number })
], WaRandomContent.prototype, "items", 2);
__decorateClass([
  n({ reflect: true })
], WaRandomContent.prototype, "mode", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaRandomContent.prototype, "autoplay", 2);
__decorateClass([
  n({ type: Number, attribute: "autoplay-interval" })
], WaRandomContent.prototype, "autoplayInterval", 2);
__decorateClass([
  n({ reflect: true })
], WaRandomContent.prototype, "animation", 2);
__decorateClass([
  watch(["autoplay", "autoplayInterval"], { waitUntilFirstUpdate: true })
], WaRandomContent.prototype, "handleAutoplayChange", 1);
__decorateClass([
  watch("mode", { waitUntilFirstUpdate: true })
], WaRandomContent.prototype, "handleModeChange", 1);
__decorateClass([
  watch("items", { waitUntilFirstUpdate: true })
], WaRandomContent.prototype, "handleItemsChange", 1);
WaRandomContent = __decorateClass([
  t("wa-random-content")
], WaRandomContent);

export {
  WaRandomContent
};
