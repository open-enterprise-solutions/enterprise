# oes-rag-local — Local RAG fallback for oes-mcp

`oes-rag-local` is a standalone binary that provides retrieval-augmented
generation (RAG) and a local LLM proxy when **Pugi cloud is unreachable**.
It is the on-prem half of the air-gapped enterprise tier: banks,
government, internal-network deployments that cannot reach
`mcp.pugi.io` but still need Sigma-style configuration help.

It is intentionally a **separate binary** from `oes-mcp`. Cloud-only
users continue to ship just `oes-mcp` — no FAISS, no Ollama, no extra
weight. Air-gapped customers ship both binaries.

---

## What ships in v1 (this version)

- Subcommands `ingest`, `serve`, `health`, `--help` — all working.
- Ingest pipeline: recursive `*.xml` walk, per-file chunking with text
  truncated at 8 KB, writes a single JSON-on-disk index
  (`*.meta.json`).
- Query server: `httplib` HTTP on `127.0.0.1`, default port `11700`,
  endpoints `POST /query`, `POST /llm`, `GET /health`.
- Brute-force **lexical retrieval** — case-insensitive substring scoring
  over chunk text. ASCII-only lowercasing (Cyrillic terms match
  case-sensitively for now; see v2).
- Ollama HTTP client — `Ping`, `Embed`, `Chat` shims. `/llm` proxies to
  `POST /api/chat` (non-streaming).
- oes-mcp degradation chain — `PugiHttpInvoke` checks
  `OES_MCP_RAG_FALLBACK_URL` on transport errors and 5xx responses, and
  attempts the local sidecar for `llm_query`, `sigma_check`, and the
  four `oes_template_*` tools before falling open to the offline
  envelope.

## What is deferred to v2

- **Real FAISS index** — currently we walk the chunk list and score with
  substring matching. Fine for the BAS_BUH corpus (~3 200 objects →
  ~6 400 chunks, sub-10 ms locally). At >100 k chunks or
  latency-sensitive deployments we vendor `faiss-cpp` and persist a
  companion `*.faiss` file.
- **Real Ollama embeddings during ingest** — v1 writes empty embedding
  arrays. v2 round-trips each chunk through `/api/embeddings` (e.g.
  `nomic-embed-text`, 768-dim) and stores them inline.
- **Per-object XML chunking** — v1 ingests whole files. v2 parses each
  `Catalog`, `Document`, `Register`, etc. into its own chunk with
  synonym/comment fields extracted, and pulls in companion `.bsl`
  modules.
- **Streaming chat responses** — v1 collects the full Ollama reply
  before responding. v2 forwards `text/event-stream` chunks straight
  through to the caller.
- **Multi-corpus indexing** — v1 = one corpus per index. v2 = labeled
  namespaces inside a single index file.
- **Unicode-aware tokenization** — switch to ICU lower-case for the
  Cyrillic case-insensitive path.

## FAISS-alternative rationale

Vendoring `faiss-cpp` doubles the binary size and adds a non-trivial
dependency surface (BLAS, OpenMP) to every CI machine. The BAS_BUH
corpus has ~6 400 chunks — a single brute-force pass over all of them
fits comfortably in the < 100 ms budget the oes-mcp fallback chain
allots before timing out. FAISS becomes worthwhile only above ~50 k
chunks; we will revisit when a customer brings a corpus that large.

## Mock embeddings rationale

The v1 ingest pipeline pings Ollama once for diagnostics but does not
round-trip per-chunk. Two reasons:

1. **Ingest can run on machines where Ollama isn't installed yet**
   (cold-bootstrap on a new air-gapped host).
2. **Lexical retrieval is already good enough** for the v1 use case —
   pointing the LLM at the right `Catalog` / `Register` definition
   based on user-supplied keywords. Once we move to real embeddings the
   gain is on **semantic** queries (`"how do I track stock"` matching
   `"AccumulationRegister"` even without the literal word).

If you want lexical results to stop showing zero embeddings in the
index file, treat the field as a v2 placeholder.

## Setup (air-gapped deployment)

```bash
# 1. Install Ollama on the same host (or accessible inside the
#    internal network).
curl -fsSL https://ollama.com/install.sh | sh

# 2. Pull a chat model + an embedding model. nomic-embed-text is what
#    the v2 ingest pipeline will use; you only need it eventually.
ollama pull qwen2.5:14b-instruct
ollama pull nomic-embed-text   # (v2 only)

# 3. Ingest your OES/BAS XML corpus.
oes-rag-local ingest /path/to/corpus --out /var/lib/oes-rag/index.meta.json

# 4. Run the sidecar (typically under systemd / launchd / a Windows
#    service — runs in the foreground here for clarity).
oes-rag-local serve /var/lib/oes-rag/index.meta.json --port 11700 \
    --ollama http://localhost:11434

# 5. Wire oes-mcp into the chain — these env vars are picked up at
#    every PugiHttpInvoke call site.
export OES_MCP_RAG_FALLBACK_URL=http://localhost:11700
export OES_MCP_RAG_FALLBACK_TIMEOUT_MS=2000

# Designer / MCP clients now degrade through:
#   Pugi cloud  →  oes-rag-local sidecar  →  offline envelope
```

## HTTP surface

### `POST /query`

```json
{ "text": "Контрагенты", "topK": 5 }
```

Returns:

```json
{
  "ok": true,
  "query": "Контрагенты",
  "topK": 5,
  "count": 1,
  "results": [
    {
      "id": "sample.xml",
      "source": "/path/to/corpus/sample.xml",
      "score": 1.0,
      "chunk": "<Catalog name=\"Контрагенты\">..."
    }
  ],
  "mode": "lexical-only",
  "_source": "local-rag-fallback"
}
```

### `POST /llm`

```json
{ "prompt": "...", "model": "qwen2.5:14b-instruct" }
```

Returns the Pugi-llm_query-shaped envelope so the oes-mcp side stamps
`_source` and passes through. Forwarded to Ollama `/api/chat` under the
hood.

### `GET /health`

`200` when index loaded **and** Ollama reachable.
`503` otherwise — body always includes `chunks`, `loaded`, `ollamaUp`,
`ollamaErr` for diagnostics.

## Wire-up with oes-mcp

The degradation chain lives in `src/engine/mcp-server/tools.cpp` inside
`PugiHttpInvoke`. It triggers when:

- `OES_MCP_RAG_FALLBACK_URL` is set in the environment, AND
- the tool being invoked is in the RAG-friendly allow-list
  (`llm_query`, `sigma_check`, `oes_templates_list`, `oes_template_get`,
  `oes_template_customize`, `oes_demo_data_get`), AND
- the Pugi cloud call either timed out / had a transport error, or
  returned `5xx`.

`4xx` responses skip the local fallback — those are client-side
validation errors (bad input, auth failure) and re-trying them locally
would mask the real problem.

Local responses always get `_source = "local-rag-fallback"` stamped
into their `structuredContent` (or as a top-level marker if
`structuredContent` is absent) so callers can branch on cloud vs local.

## When NOT to use the local fallback

- **Pure metadata tools** (`meta_query`, `meta_create`, etc.) are
  already local-only — they never round-trip to Pugi and the fallback
  chain is bypassed entirely.
- **Production cloud users** — leave `OES_MCP_RAG_FALLBACK_URL` unset.
  The fallback chain is a no-op without it.
- **Public internet servers** — `oes-rag-local serve` binds
  `127.0.0.1` only. If you genuinely need a remote-accessible sidecar,
  put it behind nginx + auth; do NOT expose its port to the open
  internet.

## Quick smoke test

```bash
mkdir -p /tmp/sample-corpus && cat > /tmp/sample-corpus/c.xml <<EOF
<?xml version="1.0"?>
<Catalog name="Контрагенты"><attribute name="Code"/></Catalog>
EOF

oes-rag-local ingest /tmp/sample-corpus
# -> {"ok":true,"chunks":1,...}

oes-rag-local serve /tmp/sample-corpus/oes-rag.meta.json --port 11700 &
sleep 1
curl -s http://127.0.0.1:11700/health
curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"text":"Контрагенты","topK":3}' \
    http://127.0.0.1:11700/query
```
