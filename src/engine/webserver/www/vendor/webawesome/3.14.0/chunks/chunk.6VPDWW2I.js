/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/components/time-input/internal/time-segments.ts
var layoutCache = /* @__PURE__ */ new Map();
function clearTimeSegmentLayoutCache() {
  layoutCache.clear();
}
function buildTimeSegmentLayout(locale, opts) {
  const key = `${locale || "en"}|${opts.hour12 ? 12 : 24}|${opts.withSeconds ? 1 : 0}`;
  const cached = layoutCache.get(key);
  if (cached) return cached;
  const formatter = new Intl.DateTimeFormat(locale || "en", {
    hour: "2-digit",
    minute: "2-digit",
    second: opts.withSeconds ? "2-digit" : void 0,
    hour12: opts.hour12,
    calendar: "gregory",
    numberingSystem: "latn"
  });
  const probeDate = new Date(2026, 0, 1, 13, 45, 30);
  const parts = formatter.formatToParts(probeDate);
  const tokens = [];
  const order = [];
  for (const part of parts) {
    if (part.type === "hour") {
      tokens.push({ kind: "segment", field: "hour" });
      order.push("hour");
    } else if (part.type === "minute") {
      tokens.push({ kind: "segment", field: "minute" });
      order.push("minute");
    } else if (part.type === "second") {
      tokens.push({ kind: "segment", field: "second" });
      order.push("second");
    } else if (part.type === "dayPeriod") {
      tokens.push({ kind: "segment", field: "dayPeriod" });
      order.push("dayPeriod");
    } else if (part.type === "literal") {
      tokens.push({ kind: "literal", text: part.value });
    }
  }
  const expectedSegments = 2 + (opts.withSeconds ? 1 : 0) + (opts.hour12 ? 1 : 0);
  if (order.length !== expectedSegments) {
    const fallbackTokens = [
      { kind: "segment", field: "hour" },
      { kind: "literal", text: ":" },
      { kind: "segment", field: "minute" }
    ];
    const fallbackOrder = ["hour", "minute"];
    if (opts.withSeconds) {
      fallbackTokens.push({ kind: "literal", text: ":" });
      fallbackTokens.push({ kind: "segment", field: "second" });
      fallbackOrder.push("second");
    }
    if (opts.hour12) {
      fallbackTokens.push({ kind: "literal", text: " " });
      fallbackTokens.push({ kind: "segment", field: "dayPeriod" });
      fallbackOrder.push("dayPeriod");
    }
    const fallback = { tokens: fallbackTokens, order: fallbackOrder };
    layoutCache.set(key, fallback);
    return fallback;
  }
  const layout = { tokens, order };
  layoutCache.set(key, layout);
  return layout;
}
function resolveHour12(locale) {
  try {
    const probe = new Intl.DateTimeFormat(locale || "en", { hour: "numeric" });
    return probe.resolvedOptions().hour12 ?? false;
  } catch {
    return false;
  }
}
function formatDayPeriod(locale, period) {
  try {
    const formatter = new Intl.DateTimeFormat(locale || "en", { hour: "numeric", hour12: true });
    const date = new Date(2026, 0, 1, period === 0 ? 9 : 15);
    const parts = formatter.formatToParts(date);
    const dp = parts.find((p) => p.type === "dayPeriod");
    return dp?.value || (period === 0 ? "AM" : "PM");
  } catch {
    return period === 0 ? "AM" : "PM";
  }
}
function timeSegmentBounds(field, hour12) {
  if (field === "hour") return hour12 ? { min: 1, max: 12 } : { min: 0, max: 23 };
  if (field === "minute" || field === "second") return { min: 0, max: 59 };
  return { min: 0, max: 1 };
}
function stepTimeSegment(segments, field, delta, hour12, now = /* @__PURE__ */ new Date()) {
  const next = { ...segments };
  const current = segments[field];
  if (field === "dayPeriod") {
    const seed = current == null ? now.getHours() < 12 ? 0 : 1 : current;
    next.dayPeriod = seed === 0 ? 1 : 0;
    return next;
  }
  const { min, max } = timeSegmentBounds(field, hour12);
  if (current == null) {
    if (field === "hour") {
      const h24 = now.getHours();
      next.hour = hour12 ? h24 % 12 || 12 : h24;
    } else if (field === "minute") {
      next.minute = now.getMinutes();
    } else {
      next.second = now.getSeconds();
    }
    return next;
  }
  const span = max - min + 1;
  const stepped = ((current - min + delta) % span + span) % span + min;
  if (field === "hour") next.hour = stepped;
  else if (field === "minute") next.minute = stepped;
  else next.second = stepped;
  return next;
}
function typeTimeDigit(field, buffer, digit, hour12) {
  if (!/^[0-9]$/.test(digit)) return { value: bufferToValue(buffer), buffer, advance: false };
  if (field === "dayPeriod") return { value: bufferToValue(buffer), buffer, advance: false };
  if (field === "hour") {
    if (hour12) return typeNumericSegment(buffer, digit, 1, 12);
    return typeNumericSegment(buffer, digit, 0, 23);
  }
  return typeNumericSegment(buffer, digit, 0, 59);
}
function typeNumericSegment(buffer, digit, min, max) {
  const d = Number(digit);
  if (buffer === "") {
    if (d === 0 && min === 0) return { value: 0, buffer: "0", advance: false };
    if (d === 0) return { value: null, buffer: "0", advance: false };
    if (d * 10 > max) {
      return { value: clamp(d, min, max), buffer: "", advance: true };
    }
    return { value: d, buffer: digit, advance: false };
  }
  const combined = Number(buffer + digit);
  if (combined >= min && combined <= max) {
    return { value: combined, buffer: "", advance: true };
  }
  if (buffer === "0" && d === 0) {
    return { value: min === 0 ? 0 : null, buffer: "0", advance: false };
  }
  return typeNumericSegment("", digit, min, max);
}
function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}
function bufferToValue(buffer) {
  if (!buffer) return null;
  const n = Number(buffer);
  return Number.isFinite(n) ? n : null;
}
function dayPeriodFromKey(key) {
  if (key === "a" || key === "A") return 0;
  if (key === "p" || key === "P") return 1;
  return null;
}
function formatTimeSegmentText(field, value, buffer, placeholder, locale) {
  if (field === "dayPeriod") {
    if (value == null) return placeholder;
    return formatDayPeriod(locale, value);
  }
  if (buffer) return buffer.padStart(2, "0");
  if (value == null) return placeholder;
  return String(value).padStart(2, "0");
}
function isTimeComplete(segments, opts) {
  if (segments.hour == null || segments.minute == null) return false;
  if (opts.withSeconds && segments.second == null) return false;
  if (opts.hour12 && segments.dayPeriod == null) return false;
  return true;
}
function isTimeEmpty(segments) {
  return segments.hour == null && segments.minute == null && segments.second == null && segments.dayPeriod == null;
}
function timeSegmentsToWire(segments, opts) {
  if (!isTimeComplete(segments, opts)) return "";
  let h24 = segments.hour;
  if (opts.hour12) {
    const period = segments.dayPeriod;
    h24 = h24 === 12 ? period === 0 ? 0 : 12 : period === 1 ? h24 + 12 : h24;
  }
  if (h24 < 0 || h24 > 23) return "";
  const min = segments.minute;
  if (min < 0 || min > 59) return "";
  const hh = String(h24).padStart(2, "0");
  const mm = String(min).padStart(2, "0");
  if (!opts.withSeconds) return `${hh}:${mm}`;
  const sec = segments.second;
  if (sec < 0 || sec > 59) return "";
  const ss = String(sec).padStart(2, "0");
  return `${hh}:${mm}:${ss}`;
}
function wireToTimeSegments(wire, opts) {
  const empty = { hour: null, minute: null, second: null, dayPeriod: null };
  if (!wire) return empty;
  const match = /^(\d{1,2}):(\d{2})(?::(\d{2}(?:\.\d+)?))?$/.exec(wire);
  if (!match) return empty;
  const h24 = Number(match[1]);
  const min = Number(match[2]);
  const sec = match[3] != null ? Math.trunc(Number(match[3])) : null;
  if (!Number.isFinite(h24) || !Number.isFinite(min)) return empty;
  if (h24 < 0 || h24 > 23 || min < 0 || min > 59) return empty;
  if (sec != null && (sec < 0 || sec > 59)) return empty;
  let displayHour;
  let dayPeriod = null;
  if (opts.hour12) {
    dayPeriod = h24 >= 12 ? 1 : 0;
    displayHour = h24 % 12 || 12;
  } else {
    displayHour = h24;
  }
  return {
    hour: displayHour,
    minute: min,
    second: opts.withSeconds ? sec ?? 0 : null,
    dayPeriod: opts.hour12 ? dayPeriod : null
  };
}
function withSecondsForStep(step) {
  if (step === "any") return true;
  if (!Number.isFinite(step) || step <= 0) return false;
  return step < 60 || step % 60 !== 0;
}
function timeSegmentRules(opts) {
  const now = opts.now ?? (() => /* @__PURE__ */ new Date());
  return {
    typeDigit: (group, field, buffer, digit) => {
      const result = typeTimeDigit(field, buffer, digit, opts.hour12());
      const segments = opts.getSegments(group);
      const next = { ...segments, [field]: result.value };
      opts.setSegments(group, next);
      return result;
    },
    step: (group, field, delta) => {
      const next = stepTimeSegment(opts.getSegments(group), field, delta, opts.hour12(), now());
      opts.setSegments(group, next);
      return { value: next[field] };
    },
    bounds: (_g, field) => timeSegmentBounds(field, opts.hour12()),
    commitBuffer: (group, field, buffer) => {
      const value = bufferToValue(buffer);
      const segments = opts.getSegments(group);
      opts.setSegments(group, { ...segments, [field]: value });
      return value;
    },
    clear: (group, field) => {
      const segments = opts.getSegments(group);
      if (segments[field] == null) return false;
      opts.setSegments(group, { ...segments, [field]: null });
      return true;
    }
  };
}

export {
  clearTimeSegmentLayoutCache,
  buildTimeSegmentLayout,
  resolveHour12,
  formatDayPeriod,
  timeSegmentBounds,
  stepTimeSegment,
  typeTimeDigit,
  bufferToValue,
  dayPeriodFromKey,
  formatTimeSegmentText,
  isTimeComplete,
  isTimeEmpty,
  timeSegmentsToWire,
  wireToTimeSegments,
  withSecondsForStep,
  timeSegmentRules
};
