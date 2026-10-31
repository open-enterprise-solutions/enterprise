/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/internal/submit-on-enter.ts
function submitOnEnter(event, el) {
  const hasModifier = event.metaKey || event.ctrlKey || event.shiftKey || event.altKey;
  if (event.key === "Enter" && !hasModifier) {
    setTimeout(() => {
      if (!event.defaultPrevented && !event.isComposing) {
        submitForm(el);
      }
    });
  }
}
var submittableTags = /* @__PURE__ */ new Map([
  ["input", true],
  ["wa-input", true],
  ["wa-tag-input", true],
  ["wa-number-input", true],
  ["wa-otp-input", true],
  ["wa-slider", true]
]);
var submittableTypes = /* @__PURE__ */ new Map([
  ["button", false],
  ["checkbox", false],
  ["color", false],
  ["date", false],
  ["datetime-local", false],
  ["email", true],
  ["file", false],
  ["hidden", false],
  ["image", false],
  ["month", false],
  ["number", true],
  ["password", true],
  ["radio", false],
  ["range", false],
  ["reset", true],
  ["search", true],
  ["submit", false],
  ["tel", true],
  ["text", true],
  ["time", false],
  ["url", true],
  ["week", false]
]);
var isSubmittableElement = (el) => {
  const tagName = el?.localName;
  if (!tagName) {
    return false;
  }
  const isSubmittableTag = Boolean(submittableTags.get(tagName));
  if (!isSubmittableTag) {
    return false;
  }
  if (tagName !== "input" && tagName !== "wa-input") {
    return true;
  }
  const type = el.type;
  return Boolean(submittableTypes.get(type));
};
function submitForm(el) {
  let form = null;
  if ("form" in el) {
    form = el.form;
  }
  if (!form && "getForm" in el) {
    form = el.getForm();
  }
  if (!form) {
    return;
  }
  const formElements = Array.from(form.elements);
  let submittableFormElements = 0;
  for (const el2 of formElements) {
    if (isSubmittableElement(el2)) {
      submittableFormElements += 1;
    }
  }
  if (submittableFormElements === 1) {
    form.requestSubmit(null);
    return;
  }
  const button = formElements.find((el2) => {
    return el2.type === "submit" && !el2.matches(":disabled");
  });
  if (!button) {
    return;
  }
  if (["input", "button"].includes(button.localName)) {
    form.requestSubmit(button);
  } else {
    button.click();
  }
}

export {
  submitOnEnter,
  submitForm
};
