/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  i
} from "./chunk.JJKKSYRO.js";
import {
  t
} from "./chunk.H23DVATU.js";
import {
  e,
  f,
  i as i2,
  l
} from "./chunk.EFUXUR2V.js";
import {
  B,
  T
} from "./chunk.BKE5EYM3.js";

// node_modules/@lit-labs/ssr-client/lib/hydrate-lit-html.js
var { TemplateInstance: l2, isIterable: s, resolveDirective: d, ChildPart: c, ElementPart: p } = i;
var f2 = (e2, t2, r = {}) => {
  if (void 0 !== t2._$litPart$) throw Error("container already contains a live render");
  let n, o, i3;
  const a = [], l3 = document.createTreeWalker(t2, NodeFilter.SHOW_COMMENT);
  let s2;
  for (; null !== (s2 = l3.nextNode()); ) {
    const t3 = s2.data;
    if (t3.startsWith("lit-part")) {
      if (0 === a.length && void 0 !== n) throw Error(`There must be only one root part per container. Found a part marker (${s2}) when we already have a root part marker (${o})`);
      i3 = m(e2, s2, a, r), void 0 === n && (n = i3), o ?? (o = s2);
    } else if (t3.startsWith("lit-node")) h(s2, a, r);
    else if (t3.startsWith("/lit-part")) {
      if (1 === a.length && i3 !== n) throw Error("internal error");
      i3 = u(s2, i3, a);
    }
  }
  if (void 0 === n) {
    const e3 = t2 instanceof ShadowRoot ? "{container.host.localName}'s shadow root" : t2 instanceof DocumentFragment ? "DocumentFragment" : t2.localName;
    console.error(`There should be exactly one root part in a render container, but we didn't find any in ${e3}.`);
  }
  t2._$litPart$ = n;
};
var m = (t2, r, a, p2) => {
  let f3, m2;
  if (0 === a.length) m2 = new c(r, null, void 0, p2), f3 = t2;
  else {
    const e2 = a[a.length - 1];
    if ("template-instance" === e2.type) m2 = new c(r, null, e2.instance, p2), e2.instance._$AV.push(m2), f3 = e2.result.values[e2.instancePartIndex++], e2.templatePartIndex++;
    else if ("iterable" === e2.type) {
      m2 = new c(r, null, e2.part, p2);
      const t3 = e2.iterator.next();
      if (t3.done) throw f3 = void 0, e2.done = true, Error("Unhandled shorter than expected iterable");
      f3 = t3.value, e2.part._$AH.push(m2);
    } else m2 = new c(r, null, e2.part, p2);
  }
  if (f3 = d(m2, f3), f3 === T) a.push({ part: m2, type: "leaf" });
  else if (i2(f3)) a.push({ part: m2, type: "leaf" }), m2._$AH = f3;
  else if (e(f3)) {
    if (l(f3)) throw Error("compiled templates are not supported");
    const e2 = "lit-part " + v(f3);
    if (r.data !== e2) throw Error("Hydration value mismatch: Unexpected TemplateResult rendered to part");
    {
      const e3 = c.prototype._$AC(f3), t3 = new l2(e3, m2);
      a.push({ type: "template-instance", instance: t3, part: m2, templatePartIndex: 0, instancePartIndex: 0, result: f3 }), m2._$AH = t3;
    }
  } else s(f3) ? (a.push({ part: m2, type: "iterable", value: f3, iterator: f3[Symbol.iterator](), done: false }), m2._$AH = []) : (a.push({ part: m2, type: "leaf" }), m2._$AH = f3 ?? "");
  return m2;
};
var u = (e2, t2, r) => {
  if (void 0 === t2) throw Error("unbalanced part marker");
  t2._$AB = e2;
  const n = r.pop();
  if ("iterable" === n.type && !n.iterator.next().done) throw Error("unexpected longer than expected iterable");
  if (r.length > 0) return r[r.length - 1].part;
};
var h = (e2, t2, n) => {
  const o = /lit-node (\d+)/.exec(e2.data), i3 = parseInt(o[1]), l3 = e2.nextElementSibling;
  if (null === l3) throw Error("could not find node for attribute parts");
  l3.removeAttribute("defer-hydration");
  const s2 = t2[t2.length - 1];
  if ("template-instance" !== s2.type) throw Error("Hydration value mismatch: Primitive found where TemplateResult expected. This usually occurs due to conditional rendering that resulted in a different value or template being rendered between the server and client.");
  {
    const e3 = s2.instance;
    for (; ; ) {
      const t3 = e3._$AD.parts[s2.templatePartIndex];
      if (void 0 === t3 || t3.type !== t.ATTRIBUTE && t3.type !== t.ELEMENT || t3.index !== i3) break;
      if (t3.type === t.ATTRIBUTE) {
        const o2 = new t3.ctor(l3, t3.name, t3.strings, s2.instance, n), i4 = f(o2) ? s2.result.values[s2.instancePartIndex] : s2.result.values, d2 = !(o2.type === t.EVENT || o2.type === t.PROPERTY);
        o2._$AI(i4, o2, s2.instancePartIndex, d2), s2.instancePartIndex += t3.strings.length - 1, e3._$AV.push(o2);
      } else {
        const t4 = new p(l3, s2.instance, n);
        d(t4, s2.result.values[s2.instancePartIndex++]), e3._$AV.push(t4);
      }
      s2.templatePartIndex++;
    }
  }
};
var w = /* @__PURE__ */ new WeakMap();
var v = (e2) => {
  let t2 = w.get(e2.strings);
  if (void 0 !== t2) return t2;
  const r = new Uint32Array(2).fill(5381);
  for (const t3 of e2.strings) for (let e3 = 0; e3 < t3.length; e3++) r[e3 % 2] = 33 * r[e3 % 2] ^ t3.charCodeAt(e3);
  const n = String.fromCharCode(...new Uint8Array(r.buffer));
  return t2 = btoa(n), w.set(e2.strings, t2), t2;
};

// node_modules/@lit-labs/ssr-client/lit-element-hydrate-support.js
globalThis.litElementHydrateSupport = ({ LitElement: s2 }) => {
  const h2 = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(s2), "observedAttributes").get;
  Object.defineProperty(s2, "observedAttributes", { get() {
    return [...h2.call(this), "defer-hydration"];
  } });
  const e2 = s2.prototype.attributeChangedCallback;
  s2.prototype.attributeChangedCallback = function(t2, i3, s3) {
    "defer-hydration" === t2 && null === s3 && n.call(this), e2.call(this, t2, i3, s3);
  };
  const n = s2.prototype.connectedCallback;
  s2.prototype.connectedCallback = function() {
    this.hasAttribute("defer-hydration") || n.call(this);
  };
  const o = s2.prototype.createRenderRoot;
  s2.prototype.createRenderRoot = function() {
    return this.shadowRoot ? (this._$AG = true, this.shadowRoot) : o.call(this);
  };
  const r = Object.getPrototypeOf(s2.prototype).update;
  s2.prototype.update = function(s3) {
    const h3 = this.render();
    if (r.call(this, s3), this._$AG) {
      this._$AG = false;
      for (const t2 of this.getAttributeNames()) if (t2.startsWith("hydrate-internals-")) {
        const i3 = t2.slice(18);
        this.removeAttribute(i3), this.removeAttribute(t2);
      }
      f2(h3, this.renderRoot, this.renderOptions);
    } else B(h3, this.renderRoot, this.renderOptions);
  };
};
/*! Bundled license information:

@lit-labs/ssr-client/lib/hydrate-lit-html.js:
  (**
   * @license
   * Copyright 2019 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)
*/
