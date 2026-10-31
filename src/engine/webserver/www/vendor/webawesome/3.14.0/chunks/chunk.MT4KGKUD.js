/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */

// src/ssr/trim-outer-markers.ts
function trimOuterMarkers(renderedContent) {
  const q = "<?>";
  const startMarker = "<!--lit-part ";
  const endMarker = "<!--/lit-part-->";
  renderedContent = renderedContent.trim();
  let start;
  let end;
  if (renderedContent.startsWith(q)) {
    start = q.length;
  } else if (renderedContent.startsWith(startMarker)) {
    start = renderedContent.indexOf("-->") + 3;
  }
  if (renderedContent.endsWith(q)) {
    end = renderedContent.length - q.length;
  } else if (renderedContent.endsWith(endMarker)) {
    end = renderedContent.length - endMarker.length;
  }
  if (start || end) {
    return trimOuterMarkers(renderedContent.slice(start ?? 0, end ?? renderedContent.length));
  }
  return renderedContent;
}

export {
  trimOuterMarkers
};
