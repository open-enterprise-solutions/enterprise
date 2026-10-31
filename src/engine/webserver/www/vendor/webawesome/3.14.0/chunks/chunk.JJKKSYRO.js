/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  T,
  Z
} from "./chunk.BKE5EYM3.js";

// ../../node_modules/lit-html/private-ssr-support.js
var r = null;
var i = { boundAttributeSuffix: Z.M, marker: Z.P, markerMatch: Z.A, HTML_RESULT: Z.C, getTemplateHtml: Z.L, overrideDirectiveResolve: (e, t) => class extends e {
  _$AS(e2, r2) {
    return t(this, r2);
  }
}, patchDirectiveResolve: (e, t) => {
  if (e.prototype._$AS !== t) {
    r ?? (r = e.prototype._$AS.name);
    for (let i2 = e.prototype; i2 !== Object.prototype; i2 = Object.getPrototypeOf(i2)) if (i2.hasOwnProperty(r)) return void (i2[r] = t);
    throw Error("Internal error: It is possible that both dev mode and production mode Lit was mixed together during SSR. Please comment on the issue: https://github.com/lit/lit/issues/4527");
  }
}, setDirectiveClass(e, t) {
  e._$litDirective$ = t;
}, getAttributePartCommittedValue: (e, r2, i2) => {
  let o = T;
  return e.j = (e2) => o = e2, e._$AI(r2, e, i2), o;
}, connectedDisconnectable: (e) => ({ ...e, _$AU: true }), resolveDirective: Z.V, AttributePart: Z.H, PropertyPart: Z.B, BooleanAttributePart: Z.N, EventPart: Z.U, ElementPart: Z.F, TemplateInstance: Z.R, isIterable: Z.D, ChildPart: Z.I };

export {
  i
};
/*! Bundled license information:

lit-html/private-ssr-support.js:
  (**
   * @license
   * Copyright 2019 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)
*/
