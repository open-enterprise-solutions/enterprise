/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/internal/live-announcer.ts
var politeLog = null;
var assertiveLog = null;
var CLEAR_DELAY = 7e3;
function createLog(politeness) {
  const log = document.createElement("div");
  log.setAttribute("role", "log");
  log.setAttribute("aria-live", politeness);
  log.setAttribute("aria-relevant", "additions");
  Object.assign(log.style, {
    position: "absolute",
    width: "1px",
    height: "1px",
    margin: "-1px",
    padding: "0",
    border: "0",
    overflow: "hidden",
    clip: "rect(0 0 0 0)",
    clipPath: "inset(50%)",
    whiteSpace: "nowrap"
  });
  return log;
}
function getLog(politeness) {
  if (politeness === "assertive") {
    assertiveLog ?? (assertiveLog = document.body.appendChild(createLog("assertive")));
    return assertiveLog;
  }
  politeLog ?? (politeLog = document.body.appendChild(createLog("polite")));
  return politeLog;
}
function announce(message, politeness = "polite") {
  if (!message) return;
  const log = getLog(politeness);
  const node = document.createElement("div");
  node.textContent = message;
  log.appendChild(node);
  setTimeout(() => node.remove(), CLEAR_DELAY);
}

export {
  announce
};
