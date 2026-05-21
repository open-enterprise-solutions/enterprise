# Pugi Template Shape Issues — Feedback to Pugi Team

**Tracker created:** 2026-05-21
**Templates source:** `https://mcp.pugi.io/api/oes-mcp/invoke` (Phase 2 OES templates)
**Templates inventory:** accounting-demo, manufacturing-demo, services-demo, trade-demo

This file tracks issues discovered when OES Designer / oes-mcp Applier processes Pugi-emitted templates. Each entry actionable for Pugi team — they own the template authoring + RAG-grounded transformation pipeline.

## Issue lifecycle

1. `OPEN` — discovered, awaiting Pugi confirmation
2. `ACKNOWLEDGED` — Pugi team confirmed, working on fix
3. `FIXED` — Pugi shipped fix, OES verified
4. `WONTFIX` — Pugi team declined, OES adapts on its end

---

## Issues

(No issues filed yet. As soon as Designer template wizard sees parser fails / Σ violations on apply / customer reports, add entries here.)

### Template format

```markdown
### ISS-NNNN — <one-line summary>

- **Status:** OPEN | ACKNOWLEDGED | FIXED | WONTFIX
- **Template:** trade-demo / accounting-demo / services-demo / manufacturing-demo
- **Version observed:** 1.0.0
- **Discovered:** 2026-MM-DD
- **Reported by:** <OES user / oes-mcp tool / Designer parser>

**Symptom:**
What broke. Stack trace / error message if any.

**Reproduction:**
1. `oes_template_get id=<template> includeData=true`
2. Apply via Designer wizard → ...
3. Observed: <X>. Expected: <Y>.

**Root cause hypothesis:**
What property/shape difference seems to drive the failure.

**Pugi-side action:**
What we ask the template generator to change.

**OES-side workaround (if any):**
Temporary handling until Pugi ships.

**Resolution:**
(fill on FIXED / WONTFIX)
```

---

## Known caveats from Pugi handoff (not bugs, but flag)

These were called out in the 2026-05-21 Pugi-side handoff. Watch for symptoms:

| Caveat | Templates affected | Mitigation |
|---|---|---|
| **ChartOfAccounts shape inferred from BAS** — Pugi has no real OES sample for this kind, may not match canonical | accounting-demo | If Designer parser fails: log issue, ping Pugi `transformLegacy()` helper |
| **AccountingRegister shape inferred from BAS** | accounting-demo | Same as above |
| **Subsystem/Role/Form/Command shapes inferred** | all 4 templates | OES often defers Subsystem/Role (matches t1-002/t1-004 deferrals); Form/Command should work via standard kinds |
| **manufacturing-demo v0.2.0** — passed through `transformLegacy()` BAS bridge (92 obj legacy → canonical), other 3 templates hand-authored | manufacturing-demo | Highest fragility; expect parser hiccups on nested forms |
| **numberLength = total включая prefix** (`'ВП-00000001'` → 11) | all documents | If customer wants digits-only: pass `modifications.documentNumberFormat='digits-only'` to `oes_template_customize` |

## How to file an issue

When OES side hits a template-shape issue:

1. Capture exact response from `oes_template_get id=<X> includeData=<...>` (JSON dump)
2. Capture the Designer/Applier failure (stderr, dialog text, errorCode)
3. Identify the offending object — usually a `kind` whose schema differs from OES canonical
4. Add entry to this file under `## Issues` with next ISS-NNNN id
5. Ping Pugi team via Slack/Linear with link to this file + ISS id

## Reference

- Pugi MCP tools: `oes_templates_list`, `oes_template_get`, `oes_template_customize`, `oes_demo_data_get`, `oes_agent`, `oes_agent_resolve`, `sigma_check`, `llm_query`
- OES-side template wizard impl: `src/engine/designer/mainFrame/templateWizard*` + `templateWizardApplier.{h,cpp}` (commit 9cc344c5)
- OES-side BAS importer (related kind-mapping concerns): `src/engine/backend/migration/basMapping.{hpp,cpp}` (commit 9362e1d1)
