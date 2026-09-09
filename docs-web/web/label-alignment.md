# Label-column alignment

A form's labels line up in a column whose width is the widest label. The server
cannot compute it: it has no font metrics and no DPI. So the client measures after
layout — `alignLabelsGlobal`, on `requestAnimationFrame`, reading `offsetWidth` of
the `.statictext` nodes inside `[data-labelrow]` and setting a common minimum.

## Timing

`document.fonts.ready` is awaited before the measurement runs. A face that has
not loaded measures narrower than the same text will occupy once it has, and a
column set from that width is set wrong for the life of the form.

## The proof

`webClient/assets/harness.html` renders one form JSON — deliberately mixed label
lengths, one long enough to set the column alone — in an iframe of the real
client. Measured in Chromium, every label reports the same width:

```
labels: [384,384,384,384,384]
```

Measured again on a live document form after the component library was removed
(2026-09-07): the three labels in one group start their fields at one x, each
label 86px.

The harness contains no renderer of its own. An earlier version did — its own
text control, its own checkbox, its own alignment pass — and it proved only that
a lookalike written beside the client behaves. A page whose purpose is to verify
an implementation must not be a second one.
