/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaStepChangeEvent
} from "./chunk.MYS3OD37.js";
import {
  WaBeforeStepChangeEvent
} from "./chunk.RQUTRD4U.js";
import {
  stepper_styles_default
} from "./chunk.D5FEZLQU.js";
import {
  announce
} from "./chunk.YQNBAO2Z.js";
import {
  parseSpaceDelimitedTokens
} from "./chunk.RMZ7BVDM.js";
import {
  visually_hidden_styles_default
} from "./chunk.UK2M7WPO.js";
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
  o
} from "./chunk.TLFIX76K.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/stepper/stepper.ts
var WaStepper = class extends WebAwesomeElement {
  constructor() {
    super();
    this.localize = new LocalizeController(this);
    this.isStacked = false;
    this.activeIndex = 0;
    this.stepCount = 0;
    this.active = "";
    this.orientation = "horizontal";
    this.linear = false;
    this.clickable = false;
    this.label = "";
    if (!o) {
      this.addEventListener("click", this.handleClick);
    }
  }
  connectedCallback() {
    super.connectedCallback();
    if (o) return;
    this.updateComplete.then(() => {
      this.handleSlotChange();
      this.mutationObserver = new MutationObserver((mutations) => {
        const isOwnMutation = mutations.some((mutation) => mutation.target.closest?.("wa-stepper") === this);
        if (isOwnMutation) this.syncSteps();
      });
      this.mutationObserver.observe(this, {
        childList: true,
        subtree: true,
        attributes: true,
        attributeFilter: ["name", "completed", "loading", "disabled", "variant"]
      });
    });
    this.resizeObserver = new ResizeObserver((entries) => {
      requestAnimationFrame(() => {
        for (const entry of entries) {
          this.updateStacking(entry.borderBoxSize[0].inlineSize);
        }
      });
    });
    this.resizeObserver.observe(this);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.mutationObserver?.disconnect();
    this.resizeObserver?.disconnect();
  }
  handleSlotChange() {
    if (this.didSSR) {
      const pending = this.getAllSteps().filter((step) => step.didSSR && !step.hasUpdated).map((step) => step.updateComplete);
      if (pending.length > 0) {
        Promise.allSettled(pending).then(() => this.handleSlotChange());
        return;
      }
    }
    this.syncSteps();
  }
  // Scoped to this stepper's own light-DOM children via slot assignment, so a stepper nested inside a step's
  // description (or elsewhere in slotted content) doesn't get pulled into this one's step list.
  getAllSteps() {
    if (!this.defaultSlot) return [];
    return this.defaultSlot.assignedElements().filter((el) => el.localName === "wa-step");
  }
  /** In `linear` mode, the 0-based index of the boundary a step's position must fall within (inclusive) to be reachable. */
  getLinearBoundaryIndex(steps) {
    const firstIncompleteIndex = steps.findIndex((step) => !step.completed);
    return firstIncompleteIndex === -1 ? steps.length - 1 : firstIncompleteIndex;
  }
  isReachable(step, steps = this.getAllSteps()) {
    if (step.disabled) return false;
    if (!this.linear) return true;
    return steps.indexOf(step) <= this.getLinearBoundaryIndex(steps);
  }
  /** Recomputes each step's position/active/locked state and the stepper's own custom states. */
  syncSteps() {
    const steps = this.getAllSteps();
    if (steps.length === 0) return;
    let activeStep = steps.find((step) => step.name === this.active);
    if (!activeStep) {
      if (this.active) {
        console.warn(`A step named "${this.active}" could not be found. Falling back to the first step.`, this);
      }
      activeStep = steps[0];
    }
    const boundaryIndex = this.getLinearBoundaryIndex(steps);
    this.activeIndex = steps.indexOf(activeStep);
    this.stepCount = steps.length;
    this.updateStacking(this.getBoundingClientRect().width);
    const isVertical = this.orientation === "vertical" || this.isStacked;
    steps.forEach((step, index) => {
      step.position = index + 1;
      step.active = step === activeStep;
      step.locked = this.linear && index > boundaryIndex;
      step.clickable = this.clickable;
      const previous = steps[index - 1];
      step.connectorStartVariant = previous?.completed ? previous.variant : void 0;
      step.toggleAttribute("data-wa-step-vertical", isVertical);
    });
    this.customStates.set(
      "completed",
      steps.every((step) => step.completed)
    );
    this.customStates.set(
      "loading",
      steps.some((step) => step.loading)
    );
    this.customStates.set("stacked", isVertical);
  }
  handleStateChange() {
    this.syncSteps();
  }
  /**
   * Decides whether an `auto` stepper stacks at the given width. Each step needs roughly 6em to keep a short label
   * on one line, so the row stacks once the stepper is narrower than that times the step count, plus the gaps.
   */
  updateStacking(width) {
    if (this.orientation !== "auto" || !this.stepsEl) {
      this.isStacked = false;
      return;
    }
    const stepCount = this.getAllSteps().length;
    const fontSize = parseFloat(getComputedStyle(this).fontSize);
    const gap = parseFloat(getComputedStyle(this.stepsEl).columnGap) || 0;
    const minRowWidth = stepCount * fontSize * 6 + Math.max(0, stepCount - 1) * gap;
    this.isStacked = width < minRowWidth;
  }
  /**
   * Requests a change to the named step. Emits a cancelable `wa-before-step-change`; if not canceled, updates
   * `active`, emits `wa-step-change`, and announces the new position to assistive technology. No-ops silently if the
   * step doesn't exist, is disabled, or (in `linear` mode) isn't reachable yet.
   */
  goTo(name) {
    const steps = this.getAllSteps();
    const target = steps.find((step) => step.name === name);
    if (!target || !this.isReachable(target, steps)) return;
    const previousStep = steps.find((step) => step.active) ?? null;
    if (target === previousStep) return;
    const detail = { name: target.name, previousName: previousStep?.name ?? null, step: target, previousStep };
    const changeEvent = new WaBeforeStepChangeEvent(detail);
    this.dispatchEvent(changeEvent);
    if (changeEvent.defaultPrevented) return;
    this.active = target.name;
    this.updateComplete.then(() => {
      this.dispatchEvent(new WaStepChangeEvent(detail));
      this.announceActiveStep();
    });
  }
  /** Announces the active step's position to assistive technology via the shared light-DOM live region. */
  announceActiveStep() {
    announce(this.localize.term("stepXOfY", this.activeIndex + 1, this.stepCount), "polite");
  }
  /** Advances to the step after the active one, if any. */
  next() {
    const steps = this.getAllSteps();
    const activeIndex = steps.findIndex((step) => step.active);
    const next = steps[activeIndex + 1];
    if (next) this.goTo(next.name);
  }
  /** Goes back to the step before the active one, if any. */
  previous() {
    const steps = this.getAllSteps();
    const activeIndex = steps.findIndex((step) => step.active);
    const previous = activeIndex > 0 ? steps[activeIndex - 1] : void 0;
    if (previous) this.goTo(previous.name);
  }
  handleClick(event) {
    if (!this.clickable) return;
    const step = event.target.closest("wa-step");
    if (!step || step.closest("wa-stepper") !== this || step.disabled) return;
    this.goTo(step.name);
  }
  render() {
    const label = this.label || this.localize.term("steps");
    const body = x`
      ${this.stepCount > 0 ? x`
            <span part="summary" class="wa-visually-hidden">
              ${this.localize.term("stepXOfY", this.activeIndex + 1, this.stepCount)}
            </span>
          ` : ""}
      <ol part="steps" class="steps" role="list">
        <slot @slotchange=${this.handleSlotChange}></slot>
      </ol>
    `;
    return this.clickable ? x`<nav part="stepper" class="stepper" aria-label=${label}>${body}</nav>` : x`<div part="stepper" class="stepper" role="group" aria-label=${label}>${body}</div>`;
  }
};
WaStepper.css = [visually_hidden_styles_default, stepper_styles_default];
__decorateClass([
  e(".steps")
], WaStepper.prototype, "stepsEl", 2);
__decorateClass([
  e("slot")
], WaStepper.prototype, "defaultSlot", 2);
__decorateClass([
  r()
], WaStepper.prototype, "isStacked", 2);
__decorateClass([
  r()
], WaStepper.prototype, "activeIndex", 2);
__decorateClass([
  r()
], WaStepper.prototype, "stepCount", 2);
__decorateClass([
  n({ reflect: true })
], WaStepper.prototype, "active", 2);
__decorateClass([
  n({ reflect: true })
], WaStepper.prototype, "orientation", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaStepper.prototype, "linear", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaStepper.prototype, "clickable", 2);
__decorateClass([
  n()
], WaStepper.prototype, "label", 2);
__decorateClass([
  watch(["active", "linear", "clickable", "orientation", "isStacked"], { waitUntilFirstUpdate: true })
], WaStepper.prototype, "handleStateChange", 1);
WaStepper = __decorateClass([
  t("wa-stepper")
], WaStepper);
if (!o) {
  document.addEventListener("click", (event) => {
    const invoker = event.target.closest("[data-stepper]");
    if (!(invoker instanceof Element)) return;
    const [command, id, ...rest] = parseSpaceDelimitedTokens(invoker.getAttribute("data-stepper") || "");
    if (!id) return;
    const root = invoker.getRootNode();
    const stepper = root.getElementById(id);
    if (stepper?.localName !== "wa-stepper") {
      console.warn(`A stepper with an ID of "${id}" could not be found in this document.`);
      return;
    }
    if (command === "next") {
      stepper.next();
    } else if (command === "previous") {
      stepper.previous();
    } else if (command === "goto" && rest.length > 0) {
      stepper.goTo(rest.join(" "));
    }
  });
}

export {
  WaStepper
};
