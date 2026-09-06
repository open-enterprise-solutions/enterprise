# Label-column alignment

A form's labels line up in a column whose width is the widest label. The server
cannot compute it: it has no font metrics and no DPI. So the client measures after
layout — `alignLabelsGlobal`, on `requestAnimationFrame`, reading `offsetWidth` of
the `.statictext` nodes inside `[data-labelrow]` and setting a common minimum.

## What UI5 changes, and what it does not

The measurement is unaffected, and that is the point of how the renderers are
built: **the label stays in the light DOM.** A UI5 renderer puts its widget in a
custom element whose internals are in shadow DOM, but the label beside it is the
same `<span class="statictext">` the legacy renderer produces, marked
`data-label-host`. Nothing measures inside a control, so nothing has to see
through a shadow boundary.

What did have to change is the timing. UI5 elements upgrade asynchronously, and a
face that has not loaded measures narrower than the same text will occupy once it
has. The pass therefore waits for `document.fonts.ready` before it measures.

## The proof

`webClient/assets/harness.html` renders one form JSON — deliberately mixed label
lengths, one long enough to set the column alone — in two iframes of the real
client, `ui=legacy` and `ui=ui5`. Measured in Chromium, both frames report every
label at the same width, in all three theme/density combinations:

```
legacy  labels: [384,384,384,384,384]
ui5     labels: [384,384,384,384,384]
```

The harness contains no renderer of its own. An earlier version did — its own
text control, its own checkbox, its own alignment pass — and it proved only that a
lookalike written beside the client behaves. A page whose purpose is to verify two
implementations must not be a third.
