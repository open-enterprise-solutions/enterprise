/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  dropdown_item_styles_default
} from "./chunk.BSKW43C6.js";
import {
  warnDeprecatedSize
} from "./chunk.RPQJAXXR.js";
import {
  HasSlotController
} from "./chunk.RWNXKUCF.js";
import {
  o
} from "./chunk.3MSWQ3RG.js";
import {
  animateWithClass
} from "./chunk.L6CIKOFQ.js";
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

// src/components/dropdown-item/dropdown-item.ts
var WaDropdownItem = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.hasSlotController = new HasSlotController(this, "[default]", "start", "end");
    this.active = false;
    this.variant = "default";
    this.size = "m";
    this.checkboxAdjacent = false;
    this.submenuAdjacent = false;
    this.type = "normal";
    this.checked = false;
    this.disabled = false;
    this.submenuOpen = false;
    this.hasSubmenu = false;
    this.handleSlotChange = () => {
      this.hasSubmenu = this.hasSlotController.test("submenu");
      this.updateHasSubmenuState();
      if (this.hasSubmenu) {
        this.setAttribute("aria-haspopup", "menu");
        this.setAttribute("aria-expanded", this.submenuOpen ? "true" : "false");
      } else {
        this.removeAttribute("aria-haspopup");
        this.removeAttribute("aria-expanded");
      }
    };
    /** Prevents click events from firing on the host when the item is disabled (e.g. programmatic .click() calls). */
    this.handleHostClick = (event) => {
      if (this.disabled) {
        event.preventDefault();
        event.stopImmediatePropagation();
      }
    };
    /** Prevents click events from firing when the item is disabled. */
    this.handleClick = (event) => {
      if (this.disabled) {
        event.preventDefault();
        event.stopImmediatePropagation();
      }
    };
    /** Handles pointer enter to open the submenu on hover */
    this.handlePointerEnter = (event) => {
      if (event.pointerType === "mouse" && this.hasSubmenu && !this.disabled) {
        this.notifyParentOfOpening();
        this.submenuOpen = true;
      }
    };
  }
  handleSizeChange() {
    warnDeprecatedSize(this.localName, this.size);
  }
  connectedCallback() {
    super.connectedCallback();
    this.addEventListener?.("click", this.handleHostClick);
    this.addEventListener?.("pointerenter", this.handlePointerEnter);
    this.shadowRoot?.addEventListener?.("click", this.handleClick, { capture: true });
    this.shadowRoot?.addEventListener?.("slotchange", this.handleSlotChange);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.closeSubmenu();
    this.removeEventListener?.("click", this.handleHostClick);
    this.removeEventListener?.("pointerenter", this.handlePointerEnter);
    this.shadowRoot?.removeEventListener?.("click", this.handleClick, { capture: true });
    this.shadowRoot?.removeEventListener?.("slotchange", this.handleSlotChange);
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.setAttribute("tabindex", "-1");
    this.hasSubmenu = this.hasSlotController.test("submenu");
    this.updateHasSubmenuState();
  }
  updated(changedProperties) {
    if (changedProperties.has("active")) {
      this.setAttribute("tabindex", this.active ? "0" : "-1");
      this.customStates.set("active", this.active);
    }
    if (changedProperties.has("checked")) {
      if (this.type === "checkbox") {
        this.setAttribute("aria-checked", this.checked ? "true" : "false");
      } else {
        this.removeAttribute("aria-checked");
      }
      this.customStates.set("checked", this.checked);
    }
    if (changedProperties.has("disabled")) {
      this.setAttribute("aria-disabled", this.disabled ? "true" : "false");
      this.customStates.set("disabled", this.disabled);
    }
    if (changedProperties.has("type")) {
      if (this.type === "checkbox") {
        this.setAttribute("role", "menuitemcheckbox");
        this.setAttribute("aria-checked", this.checked ? "true" : "false");
      } else {
        this.setAttribute("role", "menuitem");
        this.removeAttribute("aria-checked");
      }
    }
    if (changedProperties.has("href") || changedProperties.has("hasSubmenu")) {
      this.customStates.set("link", this.isLink());
    }
    if (changedProperties.has("submenuOpen")) {
      this.customStates.set("submenu-open", this.submenuOpen);
      if (this.submenuOpen) {
        this.openSubmenu();
      } else {
        this.closeSubmenu();
      }
    }
  }
  /** Update the has-submenu custom state */
  updateHasSubmenuState() {
    this.customStates.set("has-submenu", this.hasSubmenu);
  }
  /** Opens the submenu. */
  async openSubmenu() {
    const submenu = this.submenuElement;
    if (!this.hasSubmenu || !submenu || !this.isConnected) return;
    this.notifyParentOfOpening();
    submenu.showPopover?.();
    submenu.hidden = false;
    submenu.setAttribute("data-visible", "");
    this.submenuOpen = true;
    this.setAttribute("aria-expanded", "true");
    await animateWithClass(submenu, "show");
    setTimeout(() => {
      const items = this.getSubmenuItems();
      if (items.length > 0) {
        items.forEach((item, index) => item.active = index === 0);
        items[0].focus({ preventScroll: true });
      }
    }, 0);
  }
  /** Notifies the parent dropdown that this item is opening its submenu */
  notifyParentOfOpening() {
    const event = new CustomEvent("submenu-opening", {
      bubbles: true,
      composed: true,
      detail: { item: this }
    });
    this.dispatchEvent(event);
    const parent = this.parentElement;
    if (parent) {
      const siblings = [...parent.children].filter(
        (el) => el !== this && el.localName === "wa-dropdown-item" && el.getAttribute("slot") === this.getAttribute("slot") && el.submenuOpen
      );
      siblings.forEach((sibling) => {
        sibling.submenuOpen = false;
      });
    }
  }
  /** Closes the submenu. */
  async closeSubmenu() {
    const submenu = this.submenuElement;
    if (!this.hasSubmenu || !submenu) return;
    this.submenuOpen = false;
    this.setAttribute("aria-expanded", "false");
    if (!submenu.hidden) {
      await animateWithClass(submenu, "hide");
      if (submenu?.isConnected) {
        submenu.hidden = true;
        submenu.removeAttribute("data-visible");
        submenu.hidePopover?.();
      }
    }
  }
  /** Determines whether the item navigates when selected. Items with submenus never navigate. */
  isLink() {
    return Boolean(this.href) && !this.hasSubmenu;
  }
  /**
   * @internal Navigates to the item's `href` by clicking the hidden link in the shadow root. The event that triggered
   * the selection is passed along so modifier keys, such as pressing Command or Control to open a new tab, are honored.
   * Note that Safari ignores modifier keys on synthetic clicks.
   */
  navigate(sourceEvent) {
    const link = this.linkElement;
    if (!this.isLink() || this.disabled || !link) {
      return;
    }
    link.dispatchEvent(
      new MouseEvent("click", {
        bubbles: false,
        cancelable: true,
        composed: false,
        altKey: sourceEvent?.altKey ?? false,
        ctrlKey: sourceEvent?.ctrlKey ?? false,
        metaKey: sourceEvent?.metaKey ?? false,
        shiftKey: sourceEvent?.shiftKey ?? false
      })
    );
  }
  /** Gets all dropdown items in the submenu. */
  getSubmenuItems() {
    return [...this.children].filter(
      (el) => el.localName === "wa-dropdown-item" && el.getAttribute("slot") === "submenu" && !el.hasAttribute("disabled")
    );
  }
  render() {
    return x`
      ${this.href ? x`
            <a
              id="link"
              href=${this.href}
              target=${o(this.target)}
              rel=${o(this.rel)}
              download=${o(this.download)}
              tabindex="-1"
              aria-hidden="true"
            ></a>
          ` : ""}
      ${this.type === "checkbox" ? x`
            <wa-icon
              id="check"
              part="checkmark"
              exportparts="svg:checkmark__svg"
              library="system"
              name="check"
            ></wa-icon>
          ` : ""}

      <span id="icon" part="icon">
        <slot name="icon"></slot>
      </span>

      <span id="label" part="label">
        <slot></slot>
      </span>

      <span id="details" part="details">
        <slot name="details"></slot>
      </span>

      ${this.hasSubmenu ? x`
            <wa-icon
              id="submenu-indicator"
              part="submenu-icon"
              exportparts="svg:submenu-icon__svg"
              library="system"
              name="chevron-right"
            ></wa-icon>
          ` : ""}
      ${this.hasSubmenu ? x`
            <div
              id="submenu"
              part="submenu"
              popover="manual"
              role="menu"
              tabindex="-1"
              aria-orientation="vertical"
              hidden
            >
              <slot name="submenu"></slot>
            </div>
          ` : ""}
    `;
  }
};
WaDropdownItem.css = dropdown_item_styles_default;
__decorateClass([
  e("#submenu")
], WaDropdownItem.prototype, "submenuElement", 2);
__decorateClass([
  e("#link")
], WaDropdownItem.prototype, "linkElement", 2);
__decorateClass([
  n({ type: Boolean })
], WaDropdownItem.prototype, "active", 2);
__decorateClass([
  n({ reflect: true })
], WaDropdownItem.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaDropdownItem.prototype, "size", 2);
__decorateClass([
  watch("size")
], WaDropdownItem.prototype, "handleSizeChange", 1);
__decorateClass([
  n({ attribute: "checkbox-adjacent", type: Boolean, reflect: true })
], WaDropdownItem.prototype, "checkboxAdjacent", 2);
__decorateClass([
  n({ attribute: "submenu-adjacent", type: Boolean, reflect: true })
], WaDropdownItem.prototype, "submenuAdjacent", 2);
__decorateClass([
  n()
], WaDropdownItem.prototype, "value", 2);
__decorateClass([
  n({ reflect: true })
], WaDropdownItem.prototype, "type", 2);
__decorateClass([
  n({ type: Boolean })
], WaDropdownItem.prototype, "checked", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaDropdownItem.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaDropdownItem.prototype, "submenuOpen", 2);
__decorateClass([
  n({ reflect: true })
], WaDropdownItem.prototype, "href", 2);
__decorateClass([
  n()
], WaDropdownItem.prototype, "target", 2);
__decorateClass([
  n()
], WaDropdownItem.prototype, "rel", 2);
__decorateClass([
  n()
], WaDropdownItem.prototype, "download", 2);
__decorateClass([
  r()
], WaDropdownItem.prototype, "hasSubmenu", 2);
WaDropdownItem = __decorateClass([
  t("wa-dropdown-item")
], WaDropdownItem);

export {
  WaDropdownItem
};
