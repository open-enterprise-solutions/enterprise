# Pugi Backend Tasks for OES Integration

Created: 2026-05-21

Scope: work owned by Pugi/Anvil backend, not OES Designer. OES-side bridge, wizard, MCP proxy tools, confidence consumption, locale normalization, and sidecar form-layout DSL are implemented on `feature/syntax-helper`.

## Current OES Contract

Endpoint used by OES:

```http
POST https://mcp.pugi.io/api/oes-mcp/invoke
Authorization: Bearer <token>
X-Tenant-Id: <tenant>
Content-Type: application/json
```

Request shape:

```json
{
  "name": "tool_name",
  "input": {}
}
```

OES already normalizes short locales before sending (`uk` -> `uk-UA`, `ru` -> `ru-RU`, `en` -> `en-US`) and accepts Pugi env aliases.

## Required Tools

Keep these stable:

- `ai_chat_query`
- `llm_query`
- `sigma_check`
- `triple_review`
- `oes_agent`
- `oes_agent_resolve`
- `oes_templates_list`
- `oes_template_get`
- `oes_template_customize`
- `oes_demo_data_get`

## Pugi Tasks

### PUGI-001: Publish tool schema endpoint

Add:

```http
GET /api/oes-mcp/tools
```

Return MCP-style tool metadata for the tools above: name, description, inputSchema, outputSchema where applicable. OES smoke tests can then verify server/client compatibility without hardcoding expected shapes.

Acceptance:

- 200 with JSON list for a valid API key.
- Stable error envelope for invalid key.
- Includes template and agent tools.

### PUGI-002: Header compatibility

Accept both tenant headers:

- `X-Tenant-Id`
- `X-Pugi-Tenant`

Normalize internally to one tenant id. Prefer `X-Tenant-Id` when both are present.

Acceptance:

- Both headers work in smoke.
- Missing tenant uses default tenant only if product policy allows it; otherwise return clear structured error.

### PUGI-003: Locale compatibility

Accept both full and short locale tokens:

- Full: `uk-UA`, `ru-RU`, `en-US`
- Short: `uk`, `ru`, `en`

Normalize short values internally. Keep strict validation after normalization.

Acceptance:

- `llm_query` and `ai_chat_query` accept all six tokens above.
- Error response lists accepted locales when invalid.

### PUGI-004: Production `sigma_check`

Replace current scaffold 503 path with real validation or a controlled mock mode.

Acceptance:

- Default production request returns a normal MCP result, not 503 scaffold.
- If mock mode is needed, gate it explicitly by env/config and return `structuredContent.mock=true`.
- On reject, include human-readable diagnostics and machine fields.

Suggested reject shape:

```json
{
  "ok": false,
  "verdict": "BLOCK",
  "confidence": 0.82,
  "diagnostics": [
    {
      "code": "SIGMA_FIELD_MISSING",
      "message": "Catalog.Контрагенты requires attribute Наименование",
      "path": "Catalog.Контрагенты.Attributes"
    }
  ]
}
```

### PUGI-005: Confidence fields

Emit one of these fields from `ai_chat_query`, `oes_agent`, and `triple_review`:

- `confidence`
- `selfConfidence`
- `suitabilityScore`
- `reliability.score`

Use 0..1 or 0..100 consistently. OES already normalizes both.

Acceptance:

- Low-confidence response below 70% triggers OES Designer suitability warning.
- High-confidence response does not.

### PUGI-006: Template shape hardening

Keep curated templates stable:

- `trade-demo`
- `manufacturing-demo`
- `services-demo`
- `accounting-demo`

Response must include:

- `id`
- `version`
- localized `name`
- localized `description`
- `structureMutations`
- optional `demoData`
- `minHostAbi`

Use `docs/pugi-template-issues.md` for OES-discovered template bugs.

### PUGI-007: Demo data tool

Ensure `oes_demo_data_get` returns data separate from structure.

Suggested shape:

```json
{
  "templateId": "trade-demo",
  "version": "1.0.0",
  "records": [
    {
      "kind": "Catalog",
      "fullName": "Catalog.Контрагенты",
      "rows": [
        { "Наименование": "ТОВ Демо", "ИНН": "1234567890" }
      ]
    }
  ]
}
```

Acceptance:

- Tool works without regenerating structure plan.
- Records reference existing objects from the matching template.

## Local Research Notes

Inspected local repository:

- `/Volumes/T9/Web/codeforge-io/ai-engine`
- Generic MCP gateway exists at `apps/gateway/src/mcp/mcp-handlers.ts`
- No direct `oes_agent`, `sigma_check`, or `oes_templates_*` implementation found there.
- `zod` is available and used in gateway MCP handlers.
- Confidence patterns already exist in `packages/core/src/consensus/*` and `packages/intent/*`.

Likely implementation location in Pugi:

- Add an OES-specific module beside the gateway MCP handlers, or
- Add a thin `/api/oes-mcp/invoke` adapter that maps `name/input` to tool handlers and reuses existing Anvil services.

## OES Already Done

- Pugi MCP proxy tools in `oes-mcp`
- Template wizard in Designer
- Agent plan/apply bridge
- Chat transport hardening
- Confidence metadata consumption
- Locale normalization before outbound calls
- XML sidecar DSL for `form_layout_read/set`
- Local fallback scaffold for selected Pugi outages

