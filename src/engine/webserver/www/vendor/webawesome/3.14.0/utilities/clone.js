/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.JHZRD2LV.js";

// src/utilities/clone.ts
function deepClone(value, seen = /* @__PURE__ */ new WeakMap()) {
  if (value === null || typeof value !== "object") {
    return value;
  }
  if (seen.has(value)) {
    return seen.get(value);
  }
  if (Array.isArray(value)) {
    const result = [];
    seen.set(value, result);
    for (const item of value) {
      result.push(deepClone(item, seen));
    }
    return result;
  }
  if (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null) {
    const result = {};
    seen.set(value, result);
    for (const key of Object.keys(value)) {
      result[key] = deepClone(value[key], seen);
    }
    return result;
  }
  return value;
}
var clone_default = deepClone;
function isPlainObject(value) {
  return typeof value === "object" && value !== null && !Array.isArray(value) && Object.getPrototypeOf(value) === Object.prototype;
}
function deepMerge(src, target) {
  const result = deepClone(src);
  for (const key in target) {
    if (Object.prototype.hasOwnProperty.call(target, key)) {
      const targetValue = target[key];
      const srcValue = result[key];
      if (isPlainObject(targetValue) && isPlainObject(srcValue)) {
        result[key] = deepMerge(srcValue, targetValue);
      } else {
        result[key] = targetValue;
      }
    }
  }
  return result;
}
export {
  deepClone,
  deepMerge,
  clone_default as default,
  isPlainObject
};
