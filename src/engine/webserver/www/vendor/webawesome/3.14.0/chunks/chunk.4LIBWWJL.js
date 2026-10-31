/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/internal/active-elements.ts
function* activeElements(activeElement = document.activeElement) {
  if (activeElement === null || activeElement === void 0) return;
  yield activeElement;
  if ("shadowRoot" in activeElement && activeElement.shadowRoot && activeElement.shadowRoot.mode !== "closed") {
    yield* activeElements(activeElement.shadowRoot.activeElement);
  }
}
function getDeepestActiveElement() {
  return [...activeElements()].pop();
}

export {
  activeElements,
  getDeepestActiveElement
};
