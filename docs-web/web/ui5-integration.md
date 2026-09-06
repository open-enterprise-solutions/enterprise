# UI5 integration — control mapping

One section per OES control: which UI5 element renders it, how each JSON field
reaches a property, and which UI5 properties have no OES counterpart. The last
list matters: those are candidates for the core, and none is added without an ADR.

The renderers live in `webClient/client.html` as a second map merged over the
legacy one when `ui=ui5` (ADR-011). No `if (ui5)` branch exists inside a legacy
renderer.

The form JSON is unchanged by any of this (ADR-007), and no vendor property name
appears in it (ADR-006): the JSON says `semantic`, the renderer says `design`.

## Button → `ui5-button`

| JSON | UI5 | Note |
|---|---|---|
| `label` | text child | |
| `semantic` | `design` | `default`→`Default`, `primary`→`Emphasized`, `transparent`→`Transparent` |
| `enabled: false` | `disabled` | |
| `picture` + `representation` | `<img>` child | `representation` 1 = picture only, 2 = text only |
| click | `POST /action/<id>` | the existing endpoint, unchanged |

No OES counterpart: `icon` / `endIcon` (UI5's own icon slot — OES carries a
picture instead), `tooltip`, `accessibleName`, `submits`, `type`.

## CheckBox → `ui5-checkbox`

| JSON | UI5 | Note |
|---|---|---|
| `label` | `text` | must be the property; a text child renders nothing |
| `value` | `checked` | |
| `value: null` / `indeterminate` | `indeterminate` + `checked` | the third state |
| `readOnly` | `readonly` | |
| `enabled: false` | `disabled` | |
| change | `POST /toggle/<id>` | |

No OES counterpart: `valueState`, `wrappingType`, `required`, `name`.

## TextCtrl → `ui5-input` inside a fixed-width group

| JSON | UI5 | Note |
|---|---|---|
| `value` | `value` | |
| `passwordMode` | `type="Password"` | |
| `readOnly` | `readonly` | distinct from disabled, visually and behaviourally |
| `enabled: false` | `disabled` | |
| `error` | `valueState="Negative"` | the JSON says `error`, never `valueState` |
| `showSelectButton` | a `…` `ui5-button` | `design="Transparent"` |
| `showOpenButton` | a `▷` `ui5-button` | |
| `showClearButton` | a `×` `ui5-button` | |
| input / change | `POST /change/<id>` | |

**The side buttons do not change the outer width.** The group is a flex box of a
fixed width; the field takes `flex: 1 1 0` with `width: 0` so it shrinks and the
buttons take their space from it. A field with three buttons and a field with none
have the same right edge — visible in the harness, where the long-caption row sits
under four others and all five box edges line up.

No OES counterpart: `valueStateMessage`, `showSuggestions`, `maxlength`,
`placeholder`, `required`, `name`.

## What every renderer has to do

Custom elements are `display: inline` until told otherwise, which quietly breaks
them as flex children of a sizer (ADR-004). Each renderer sets its own display —
`inline-block` for button and checkbox, `flex` for the text group.
