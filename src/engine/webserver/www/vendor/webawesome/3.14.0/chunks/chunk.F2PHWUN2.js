/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaLazyChangeEvent
} from "./chunk.ZSEFTQAO.js";
import {
  WaLazyLoadEvent
} from "./chunk.26QE47KB.js";
import {
  WaExpandEvent
} from "./chunk.FYKN76UA.js";
import {
  WaCollapseEvent
} from "./chunk.U36KZLSQ.js";
import {
  WaAfterCollapseEvent
} from "./chunk.AG44H7MD.js";
import {
  WaAfterExpandEvent
} from "./chunk.Q6XMGFWJ.js";
import {
  tree_item_styles_default
} from "./chunk.J7EXAHCE.js";
import {
  n as n2
} from "./chunk.YXOWVBUA.js";
import {
  l
} from "./chunk.K5EDTD7G.js";
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
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// ../../node_modules/@lit/context/lib/context-request-event.js
var s = class extends Event {
  constructor(s4, t3, e5, o) {
    super("context-request", { bubbles: true, composed: true }), this.context = s4, this.contextTarget = t3, this.callback = e5, this.subscribe = o ?? false;
  }
};

// ../../node_modules/@lit/context/lib/create-context.js
function n3(n4) {
  return n4;
}

// ../../node_modules/@lit/context/lib/controllers/context-consumer.js
var s2 = class {
  constructor(t3, s4, i2, h) {
    if (this.subscribe = false, this.provided = false, this.value = void 0, this.t = (t4, s5) => {
      this.unsubscribe && (this.unsubscribe !== s5 && (this.provided = false, this.unsubscribe()), this.subscribe || this.unsubscribe()), this.value = t4, this.host.requestUpdate(), this.provided && !this.subscribe || (this.provided = true, this.callback && this.callback(t4, s5)), this.unsubscribe = s5;
    }, this.host = t3, void 0 !== s4.context) {
      const t4 = s4;
      this.context = t4.context, this.callback = t4.callback, this.subscribe = t4.subscribe ?? false;
    } else this.context = s4, this.callback = i2, this.subscribe = h ?? false;
    this.host.addController(this);
  }
  hostConnected() {
    this.dispatchRequest();
  }
  hostDisconnected() {
    this.unsubscribe && (this.unsubscribe(), this.unsubscribe = void 0);
  }
  dispatchRequest() {
    this.host.dispatchEvent(new s(this.context, this.host, this.t, this.subscribe));
  }
};

// ../../node_modules/@lit/context/lib/value-notifier.js
var s3 = class {
  get value() {
    return this.o;
  }
  set value(s4) {
    this.setValue(s4);
  }
  setValue(s4, t3 = false) {
    const i2 = t3 || !Object.is(s4, this.o);
    this.o = s4, i2 && this.updateObservers();
  }
  constructor(s4) {
    this.subscriptions = /* @__PURE__ */ new Map(), this.updateObservers = () => {
      for (const [s5, { disposer: t3 }] of this.subscriptions) s5(this.o, t3);
    }, void 0 !== s4 && (this.value = s4);
  }
  addCallback(s4, t3, i2) {
    if (!i2) return void s4(this.value);
    this.subscriptions.has(s4) || this.subscriptions.set(s4, { disposer: () => {
      this.subscriptions.delete(s4);
    }, consumerHost: t3 });
    const { disposer: h } = this.subscriptions.get(s4);
    s4(this.value, h);
  }
  clearCallbacks() {
    this.subscriptions.clear();
  }
};

// ../../node_modules/@lit/context/lib/controllers/context-provider.js
var e3 = class extends Event {
  constructor(t3, s4) {
    super("context-provider", { bubbles: true, composed: true }), this.context = t3, this.contextTarget = s4;
  }
};
var i = class extends s3 {
  constructor(s4, e5, i2) {
    super(void 0 !== e5.context ? e5.initialValue : i2), this.onContextRequest = (t3) => {
      if (t3.context !== this.context) return;
      const s5 = t3.contextTarget ?? t3.composedPath()[0];
      s5 !== this.host && (t3.stopPropagation(), this.addCallback(t3.callback, s5, t3.subscribe));
    }, this.onProviderRequest = (s5) => {
      if (s5.context !== this.context) return;
      if ((s5.contextTarget ?? s5.composedPath()[0]) === this.host) return;
      const e6 = /* @__PURE__ */ new Set();
      for (const [s6, { consumerHost: i3 }] of this.subscriptions) e6.has(s6) || (e6.add(s6), i3.dispatchEvent(new s(this.context, i3, s6, true)));
      s5.stopPropagation();
    }, this.host = s4, void 0 !== e5.context ? this.context = e5.context : this.context = e5, this.attachListeners(), this.host.addController?.(this);
  }
  attachListeners() {
    this.host.addEventListener("context-request", this.onContextRequest), this.host.addEventListener("context-provider", this.onProviderRequest);
  }
  hostConnected() {
    this.host.dispatchEvent(new e3(this.context, this.host));
  }
};

// ../../node_modules/@lit/context/lib/decorators/provide.js
function e4({ context: e5 }) {
  return (n4, i2) => {
    const r2 = /* @__PURE__ */ new WeakMap();
    if ("object" == typeof i2) return { get() {
      return n4.get.call(this);
    }, set(t3) {
      return r2.get(this).setValue(t3), n4.set.call(this, t3);
    }, init(n5) {
      return r2.set(this, new i(this, { context: e5, initialValue: n5 })), n5;
    } };
    {
      n4.constructor.addInitializer(((n5) => {
        r2.set(n5, new i(n5, { context: e5 }));
      }));
      const o = Object.getOwnPropertyDescriptor(n4, i2);
      let s4;
      if (void 0 === o) {
        const t3 = /* @__PURE__ */ new WeakMap();
        s4 = { get() {
          return t3.get(this);
        }, set(e6) {
          r2.get(this).setValue(e6), t3.set(this, e6);
        }, configurable: true, enumerable: true };
      } else {
        const t3 = o.set;
        s4 = { ...o, set(e6) {
          r2.get(this).setValue(e6), t3?.call(this, e6);
        } };
      }
      return void Object.defineProperty(n4, i2, s4);
    }
  };
}

// ../../node_modules/@lit/context/lib/decorators/consume.js
function c({ context: c2, subscribe: e5 }) {
  return (o, n4) => {
    "object" == typeof n4 ? n4.addInitializer((function() {
      new s2(this, { context: c2, callback: (t3) => {
        o.set.call(this, t3);
      }, subscribe: e5 });
    })) : o.constructor.addInitializer(((o2) => {
      new s2(o2, { context: c2, callback: (t3) => {
        o2[n4] = t3;
      }, subscribe: e5 });
    }));
  };
}

// src/components/tree-item/tree-item.ts
var treeItemContext = n3("wa-tree-item");
var WaTreeItem = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.indeterminate = false;
    this.isLeaf = false;
    this.loading = false;
    this.selectable = false;
    this.expanded = false;
    this.selected = false;
    this.disabled = false;
    this.lazy = false;
    this._treeItemContext = { depth: 0, expanded: this.expanded };
    this._parentTreeContext = null;
    this.animationGeneration = 0;
    this.tabIndex = -1;
    this.role = "treeitem";
  }
  static isTreeItem(node) {
    const el = node;
    return el && (el.role === "treeitem" || el.getAttribute?.("role") === "treeitem");
  }
  connectedCallback() {
    super.connectedCallback();
    this.setAttribute("role", "treeitem");
    this.setAttribute("tabIndex", this.tabIndex.toString());
    if (this.isNestedItem()) {
      this.setAttribute("slot", "children");
      if (!this._parentTreeContext?.expanded) {
        this.expanded = false;
      }
    }
    if (this._parentTreeContext) {
      this._treeItemContext = { depth: this._parentTreeContext.depth + 1, expanded: this.expanded };
    }
    this.updateIndentation();
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.childrenContainer.hidden = !this.expanded;
    this.childrenContainer.style.height = this.expanded ? "auto" : "0";
    this.isLeaf = !this.lazy && this.getChildrenItems().length === 0;
    this.handleExpandedChange();
  }
  async animateCollapse(generation) {
    this.dispatchEvent(new WaCollapseEvent());
    const duration = parseDuration(getComputedStyle(this.childrenContainer).getPropertyValue("--hide-duration"));
    await animate(
      this.childrenContainer,
      [
        // We can't animate from 'auto', so use the scroll height for now
        { height: `${this.childrenContainer.scrollHeight}px`, opacity: "1", overflow: "hidden" },
        { height: "0", opacity: "0", overflow: "hidden" }
      ],
      { duration, easing: "cubic-bezier(0.4, 0.0, 0.2, 1)" }
    );
    if (this.animationGeneration !== generation) {
      return;
    }
    this.childrenContainer.hidden = true;
    this.dispatchEvent(new WaAfterCollapseEvent());
  }
  // Checks whether the item is nested into an item
  isNestedItem() {
    if (this._parentTreeContext !== null) {
      return true;
    }
    const parent = this.parentElement;
    return !!parent && WaTreeItem.isTreeItem(parent);
  }
  /** Counts the nesting depth and sets the private --indent property on the host for indentation. */
  updateIndentation() {
    const depth = Math.max(this._treeItemContext?.depth || 0, this.getDepth());
    this.setStyleProperty("--indent", `calc(${depth} * var(--indent-size, 2em))`);
  }
  getDepth() {
    let depth = 0;
    let node = this.parentElement;
    while (node) {
      if (WaTreeItem.isTreeItem(node)) {
        depth++;
      }
      node = node.parentElement;
    }
    return depth;
  }
  handleChildrenSlotChange() {
    this.loading = false;
    this.isLeaf = !this.lazy && this.getChildrenItems().length === 0;
  }
  willUpdate(changedProperties) {
    if (changedProperties.has("selected") && !changedProperties.has("indeterminate")) {
      this.indeterminate = false;
    }
    super.willUpdate(changedProperties);
  }
  async animateExpand(generation) {
    this.dispatchEvent(new WaExpandEvent());
    this.childrenContainer.hidden = false;
    const duration = parseDuration(getComputedStyle(this.childrenContainer).getPropertyValue("--show-duration"));
    await animate(
      this.childrenContainer,
      [
        { height: "0", opacity: "0", overflow: "hidden" },
        { height: `${this.childrenContainer.scrollHeight}px`, opacity: "1", overflow: "hidden" }
      ],
      {
        duration,
        easing: "cubic-bezier(0.4, 0.0, 0.2, 1)"
      }
    );
    if (this.animationGeneration !== generation) {
      return;
    }
    this.childrenContainer.style.height = "auto";
    this.dispatchEvent(new WaAfterExpandEvent());
  }
  handleLoadingChange() {
    this.setAttribute("aria-busy", this.loading ? "true" : "false");
    if (!this.loading) {
      this.animateExpand(this.animationGeneration);
    }
  }
  handleDisabledChange() {
    this.customStates.set("disabled", this.disabled);
    this.setAttribute("aria-disabled", this.disabled ? "true" : "false");
  }
  handleExpandedState() {
    this.customStates.set("expanded", this.expanded);
  }
  handleIndeterminateStateChange() {
    this.customStates.set("indeterminate", this.indeterminate);
  }
  handleSelectedChange() {
    this.customStates.set("selected", this.selected);
    this.setAttribute("aria-selected", this.selected ? "true" : "false");
  }
  handleExpandedChange() {
    if (!this.isLeaf) {
      this.setAttribute("aria-expanded", this.expanded ? "true" : "false");
    } else {
      this.removeAttribute("aria-expanded");
    }
  }
  handleExpandAnimation() {
    this.animationGeneration++;
    const generation = this.animationGeneration;
    if (this.expanded) {
      if (this.lazy) {
        this.loading = true;
        this.dispatchEvent(new WaLazyLoadEvent());
      } else {
        this.animateExpand(generation);
      }
    } else {
      this.animateCollapse(generation);
    }
  }
  handleLazyChange() {
    this.dispatchEvent(new WaLazyChangeEvent());
  }
  /** Gets all the nested tree items in this node. */
  getChildrenItems({ includeDisabled = true } = {}) {
    return this.childrenSlot ? [...this.childrenSlot.assignedElements({ flatten: true })].filter(
      (item) => WaTreeItem.isTreeItem(item) && (includeDisabled || !item.disabled)
    ) : [];
  }
  render() {
    const isRtl = this.localize.dir() === "rtl";
    const showExpandButton = !this.loading && (!this.isLeaf || this.lazy);
    return x`
      <div
        part="base tree-item"
        class="${e2({
      "tree-item": true,
      "tree-item-expanded": this.expanded,
      "tree-item-selected": this.selected,
      "tree-item-leaf": this.isLeaf,
      "tree-item-loading": this.loading,
      "tree-item-has-expand-button": showExpandButton
    })}"
      >
        <div class="item" part="item">
          <div class="indentation" part="indentation"></div>

          <div
            part="expand-button"
            class=${e2({
      "expand-button": true,
      "expand-button-visible": showExpandButton
    })}
            aria-hidden="true"
          >
            <slot class="expand-icon-slot" name="expand-icon">
              ${n2(
      this.loading,
      () => x` <wa-spinner part="spinner" exportparts="base:spinner__base"></wa-spinner> `,
      () => x`
                  <wa-icon name=${isRtl ? "chevron-left" : "chevron-right"} library="system" variant="solid"></wa-icon>
                `
    )}
            </slot>
            <slot class="expand-icon-slot" name="collapse-icon">
              <wa-icon name=${isRtl ? "chevron-left" : "chevron-right"} library="system" variant="solid"></wa-icon>
            </slot>
          </div>

          ${n2(
      this.selectable,
      () => x`
              <wa-checkbox
                part="checkbox"
                exportparts="
                    base:checkbox__base,
                    control:checkbox__control,
                    checked-icon:checkbox__checked-icon,
                    indeterminate-icon:checkbox__indeterminate-icon,
                    label:checkbox__label
                  "
                class="checkbox"
                ?disabled="${this.disabled}"
                ?checked="${l(this.selected)}"
                ?indeterminate="${this.indeterminate}"
                tabindex="-1"
              ></wa-checkbox>
            `
    )}

          <slot class="label" part="label"></slot>
        </div>

        <div class="children" part="children" role="group" ?hidden=${!this.expanded && !this.isConnected}>
          <slot name="children" @slotchange="${this.handleChildrenSlotChange}"></slot>
        </div>
      </div>
    `;
  }
};
WaTreeItem.css = tree_item_styles_default;
__decorateClass([
  r()
], WaTreeItem.prototype, "indeterminate", 2);
__decorateClass([
  r()
], WaTreeItem.prototype, "isLeaf", 2);
__decorateClass([
  r()
], WaTreeItem.prototype, "loading", 2);
__decorateClass([
  r()
], WaTreeItem.prototype, "selectable", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "expanded", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "selected", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "lazy", 2);
__decorateClass([
  e4({ context: treeItemContext })
], WaTreeItem.prototype, "_treeItemContext", 2);
__decorateClass([
  c({ context: treeItemContext, subscribe: false })
], WaTreeItem.prototype, "_parentTreeContext", 2);
__decorateClass([
  e("slot:not([name])")
], WaTreeItem.prototype, "defaultSlot", 2);
__decorateClass([
  e("slot[name=children]")
], WaTreeItem.prototype, "childrenSlot", 2);
__decorateClass([
  e(".item")
], WaTreeItem.prototype, "itemElement", 2);
__decorateClass([
  e(".children")
], WaTreeItem.prototype, "childrenContainer", 2);
__decorateClass([
  e(".expand-button slot")
], WaTreeItem.prototype, "expandButtonSlot", 2);
__decorateClass([
  n({ reflect: true, type: Number, attribute: "tabindex" })
], WaTreeItem.prototype, "tabIndex", 2);
__decorateClass([
  n({ reflect: true })
], WaTreeItem.prototype, "role", 2);
__decorateClass([
  watch("loading", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleLoadingChange", 1);
__decorateClass([
  watch("disabled")
], WaTreeItem.prototype, "handleDisabledChange", 1);
__decorateClass([
  watch("expanded")
], WaTreeItem.prototype, "handleExpandedState", 1);
__decorateClass([
  watch("indeterminate")
], WaTreeItem.prototype, "handleIndeterminateStateChange", 1);
__decorateClass([
  watch("selected")
], WaTreeItem.prototype, "handleSelectedChange", 1);
__decorateClass([
  watch("expanded", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleExpandedChange", 1);
__decorateClass([
  watch("expanded", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleExpandAnimation", 1);
__decorateClass([
  watch("lazy", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleLazyChange", 1);
WaTreeItem = __decorateClass([
  t("wa-tree-item")
], WaTreeItem);
WaTreeItem.disableWarning?.("change-in-update");

export {
  treeItemContext,
  WaTreeItem
};
/*! Bundled license information:

@lit/context/lib/context-request-event.js:
@lit/context/lib/create-context.js:
@lit/context/lib/controllers/context-consumer.js:
@lit/context/lib/value-notifier.js:
@lit/context/lib/controllers/context-provider.js:
@lit/context/lib/context-root.js:
  (**
   * @license
   * Copyright 2021 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)

@lit/context/lib/decorators/provide.js:
  (**
   * @license
   * Copyright 2017 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)

@lit/context/lib/decorators/consume.js:
  (**
   * @license
   * Copyright 2022 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)
*/
