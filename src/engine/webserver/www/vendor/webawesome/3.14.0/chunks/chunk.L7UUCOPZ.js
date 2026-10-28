/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  drawer_styles_default
} from "./chunk.CV35G2DF.js";
import {
  RenderedWatcher
} from "./chunk.UMPSPH77.js";
import {
  isEventInsideRect,
  lockBodyScrolling,
  unlockBodyScrolling
} from "./chunk.DITA2S5A.js";
import {
  parseSpaceDelimitedTokens
} from "./chunk.RMZ7BVDM.js";
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
  isTopDismissible,
  registerDismissible,
  unregisterDismissible
} from "./chunk.3H2FQERJ.js";
import {
  HasSlotController
} from "./chunk.RWNXKUCF.js";
import {
  o as o2
} from "./chunk.3MSWQ3RG.js";
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

// src/components/drawer/drawer.ts
var WaDrawer = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.hasSlotController = new HasSlotController(this, "footer", "header-actions", "label");
    this.renderedWatcher = new RenderedWatcher(this, (isRendered) => this.handleRenderedChange(isRendered));
    this.open = false;
    this.label = "";
    this.placement = "end";
    this.withoutHeader = false;
    this.lightDismiss = false;
    this.withFooter = false;
    this.withLabel = false;
    this.handleDocumentKeyDown = (event) => {
      if (event.key === "Escape" && this.open && isTopDismissible(this)) {
        event.preventDefault();
        event.stopPropagation();
        this.requestClose(this.drawer);
      }
    };
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    if (this.open) {
      this.addOpenListeners();
      this.drawer.showModal();
      lockBodyScrolling(this);
      this.renderedWatcher.start(this.drawer);
    }
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.renderedWatcher.stop();
    unlockBodyScrolling(this);
    this.removeOpenListeners();
  }
  async requestClose(source) {
    const waHideEvent = new WaHideEvent({ source });
    this.dispatchEvent(waHideEvent);
    if (waHideEvent.defaultPrevented) {
      this.open = true;
      animateWithClass(this.drawer, "pulse");
      return;
    }
    this.removeOpenListeners();
    await animateWithClass(this.drawer, "hide");
    this.open = false;
    this.drawer.close();
    unlockBodyScrolling(this);
    this.renderedWatcher.stop();
    const trigger = this.originalTrigger;
    if (typeof trigger?.focus === "function") {
      setTimeout(() => trigger.focus());
    }
    this.dispatchEvent(new WaAfterHideEvent());
  }
  addOpenListeners() {
    document.addEventListener("keydown", this.handleDocumentKeyDown);
    registerDismissible(this);
  }
  removeOpenListeners() {
    document.removeEventListener("keydown", this.handleDocumentKeyDown);
    unregisterDismissible(this);
  }
  handleDialogCancel(event) {
    event.preventDefault();
    if (!this.drawer.classList.contains("hide") && event.target === this.drawer && isTopDismissible(this)) {
      this.requestClose(this.drawer);
    }
  }
  handleDialogClick(event) {
    const target = event.target;
    const button = target.closest('[data-drawer="close"]');
    if (button) {
      event.stopPropagation();
      this.requestClose(button);
    }
  }
  async handleDialogPointerDown(event) {
    if (event.target === this.drawer && !isEventInsideRect(event, this.drawer)) {
      if (this.lightDismiss) {
        this.requestClose(this.drawer);
      } else {
        await animateWithClass(this.drawer, "pulse");
      }
    }
  }
  /**
   * Suspends the modal when third-party CSS (e.g. cookie banner blockers) hides an open drawer, so the page isn't
   * left scroll locked and inert. "open" stays true so the modal resumes if the drawer is rendered again.
   */
  handleRenderedChange(isRendered) {
    if (!this.open) {
      this.renderedWatcher.stop();
      return;
    }
    if (!isRendered && this.drawer.open) {
      this.removeOpenListeners();
      this.drawer.close();
      unlockBodyScrolling(this);
    } else if (isRendered && !this.drawer.open) {
      this.addOpenListeners();
      this.drawer.showModal();
      lockBodyScrolling(this);
    }
  }
  handleOpenChange() {
    if (this.open && !this.drawer.open) {
      this.show();
    } else if (this.drawer.open) {
      this.open = true;
      this.requestClose(this.drawer);
    } else if (!this.open) {
      this.renderedWatcher.stop();
    }
  }
  /** Shows the drawer. */
  async show() {
    const waShowEvent = new WaShowEvent();
    this.dispatchEvent(waShowEvent);
    if (waShowEvent.defaultPrevented) {
      this.open = false;
      return;
    }
    this.addOpenListeners();
    this.originalTrigger = document.activeElement;
    this.open = true;
    this.drawer.showModal();
    lockBodyScrolling(this);
    this.renderedWatcher.start(this.drawer);
    requestAnimationFrame(() => {
      const elementToFocus = this.querySelector("[autofocus]");
      if (elementToFocus && typeof elementToFocus.focus === "function") {
        elementToFocus.focus();
      } else {
        this.drawer.focus();
      }
    });
    await animateWithClass(this.drawer, "show");
    this.dispatchEvent(new WaAfterShowEvent());
  }
  render() {
    const hasHeader = !this.withoutHeader;
    const hasFooter = this.hasSlotController.test("footer", "withFooter");
    const hasLabel = this.label.length > 0 || this.hasSlotController.test("label", "withLabel");
    return x`
      <dialog
        part="dialog"
        aria-labelledby=${o2(hasHeader && hasLabel ? "title" : void 0)}
        aria-label=${o2(!hasHeader && this.label ? this.label : void 0)}
        class=${e2({
      drawer: true,
      open: this.open,
      top: this.placement === "top",
      end: this.placement === "end",
      bottom: this.placement === "bottom",
      start: this.placement === "start"
    })}
        @cancel=${this.handleDialogCancel}
        @click=${this.handleDialogClick}
        @pointerdown=${this.handleDialogPointerDown}
      >
        ${hasHeader ? x`
              <div part="header" class="header">
                <h2 part="title" class="title" id="title">
                  <!-- If there's no label, use an invisible character to prevent the header from collapsing -->
                  <slot name="label"> ${this.label.length > 0 ? this.label : String.fromCharCode(8203)} </slot>
                </h2>
                <div part="header-actions" class="header-actions">
                  <slot name="header-actions"></slot>
                  <wa-button
                    part="close-button"
                    exportparts="base:close-button__base"
                    class="close"
                    appearance="plain"
                    @click="${(event) => this.requestClose(event.target)}"
                  >
                    <wa-icon
                      name="xmark"
                      label=${this.localize.term("close")}
                      library="system"
                      variant="solid"
                    ></wa-icon>
                  </wa-button>
                </div>
              </div>
            ` : ""}

        <div part="body" class="body"><slot></slot></div>

        <div part="footer" class="footer" ?hidden=${!hasFooter}>
          <slot name="footer"></slot>
        </div>
      </dialog>
    `;
  }
};
WaDrawer.css = drawer_styles_default;
__decorateClass([
  e(".drawer")
], WaDrawer.prototype, "drawer", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaDrawer.prototype, "open", 2);
__decorateClass([
  n({ reflect: true })
], WaDrawer.prototype, "label", 2);
__decorateClass([
  n({ reflect: true })
], WaDrawer.prototype, "placement", 2);
__decorateClass([
  n({ attribute: "without-header", type: Boolean, reflect: true })
], WaDrawer.prototype, "withoutHeader", 2);
__decorateClass([
  n({ attribute: "light-dismiss", type: Boolean })
], WaDrawer.prototype, "lightDismiss", 2);
__decorateClass([
  n({ attribute: "with-footer", type: Boolean })
], WaDrawer.prototype, "withFooter", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaDrawer.prototype, "withLabel", 2);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaDrawer.prototype, "handleOpenChange", 1);
WaDrawer = __decorateClass([
  t("wa-drawer")
], WaDrawer);
if (!o) {
  document.addEventListener("click", (event) => {
    const drawerAttrEl = event.target.closest("[data-drawer]");
    if (drawerAttrEl instanceof Element) {
      const [command, id] = parseSpaceDelimitedTokens(drawerAttrEl.getAttribute("data-drawer") || "");
      if (command === "open" && id?.length) {
        const doc = drawerAttrEl.getRootNode();
        const drawer = doc.getElementById(id);
        if (drawer?.localName === "wa-drawer") {
          drawer.open = true;
        } else {
          console.warn(`A drawer with an ID of "${id}" could not be found in this document.`);
        }
      }
    }
  });
  document.addEventListener("pointerdown", () => {
  });
}

export {
  WaDrawer
};
