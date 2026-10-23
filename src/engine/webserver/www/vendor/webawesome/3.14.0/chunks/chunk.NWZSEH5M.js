/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaIncludeErrorEvent
} from "./chunk.H7TA73OO.js";
import {
  include_styles_default
} from "./chunk.5KKHTQSP.js";
import {
  requestInclude
} from "./chunk.WGFDW2LC.js";
import {
  WaLoadEvent
} from "./chunk.WDIIGUNP.js";
import {
  watch
} from "./chunk.PZAN6FPN.js";
import {
  WebAwesomeElement,
  n,
  t
} from "./chunk.LBLI4KS5.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// src/components/include/include.ts
var WaInclude = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.mode = "cors";
    this.allowScripts = false;
  }
  executeScript(script) {
    const newScript = document.createElement("script");
    [...script.attributes].forEach((attr) => newScript.setAttribute(attr.name, attr.value));
    newScript.textContent = script.textContent;
    script.parentNode.replaceChild(newScript, script);
  }
  /** Clones the contents of an element — a template's `content`, or any other element's children — for insertion. */
  cloneFragment(element, ownerDocument) {
    const content = element.localName === "template" ? element.content : this.childNodesToFragment(element);
    return ownerDocument.importNode(content, true);
  }
  childNodesToFragment(element) {
    const fragment = element.ownerDocument.createDocumentFragment();
    element.childNodes.forEach((child) => fragment.append(child.cloneNode(true)));
    return fragment;
  }
  async handleSrcChange() {
    try {
      const src = this.src;
      const url = new URL(src, document.baseURI);
      const fragmentId = url.hash.slice(1);
      if (src.startsWith("#")) {
        const element = fragmentId ? document.getElementById(decodeURIComponent(fragmentId)) : null;
        if (element) {
          this.replaceChildren(this.cloneFragment(element, document));
        } else {
          this.replaceChildren();
        }
        this.dispatchEvent(new WaLoadEvent());
        return;
      }
      let fetchSrc = src;
      if (fragmentId) {
        url.hash = "";
        fetchSrc = url.href;
      }
      const file = await requestInclude(fetchSrc, this.mode);
      if (src !== this.src) {
        return;
      }
      if (!file.ok) {
        this.dispatchEvent(new WaIncludeErrorEvent({ status: file.status }));
        return;
      }
      if (fragmentId) {
        const doc = new DOMParser().parseFromString(file.html, "text/html");
        const element = doc.getElementById(decodeURIComponent(fragmentId));
        if (!element) {
          this.dispatchEvent(new WaIncludeErrorEvent({ status: file.status }));
          return;
        }
        this.replaceChildren(this.cloneFragment(element, document));
      } else {
        this.innerHTML = file.html;
      }
      if (this.allowScripts) {
        [...this.querySelectorAll("script")].forEach((script) => this.executeScript(script));
      }
      this.dispatchEvent(new WaLoadEvent());
    } catch {
      this.dispatchEvent(new WaIncludeErrorEvent({ status: -1 }));
    }
  }
  render() {
    return x`<slot></slot>`;
  }
};
WaInclude.css = include_styles_default;
__decorateClass([
  n()
], WaInclude.prototype, "src", 2);
__decorateClass([
  n()
], WaInclude.prototype, "mode", 2);
__decorateClass([
  n({ attribute: "allow-scripts", type: Boolean })
], WaInclude.prototype, "allowScripts", 2);
__decorateClass([
  watch("src")
], WaInclude.prototype, "handleSrcChange", 1);
WaInclude = __decorateClass([
  t("wa-include")
], WaInclude);

export {
  WaInclude
};
