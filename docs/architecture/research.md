# Research orchestrator

[中文](research.zh-CN.md)

Engine-owned research loop with two stages: **requirements discovery** then **deep research**.

## Boundary

| Layer | Owns |
|-------|------|
| **SDK** | Discovery → lock → deep research (depth layers, knowledge graph, module routing), MCP tools, evidence DB, XAIOP phases. |
| **Client** | Submit query, poll/cancel/continue, center choices + report, right-rail live feed, **report export** (md/pdf/docx). |

## Phase 1 — requirements discovery

1. User submits initial query.
2. Engine may search Bocha / GitHub **to clarify the need** (not to answer it).
3. Model emits `thinking`, then `search` / `ask_user` / `confirm`.
4. **`ask_user` policy:** ask only for necessary unknowns; **never guess**. If the model emits `ask_user`, the engine presents it **verbatim** (no option rewriting, no ask quota). After the user replies, the model continues the loop (search / ask again / confirm).
5. When the model emits `confirm`, the engine emits **`confirm_need`** and waits for an explicit user Confirm (or adjustment). Only then does it lock and enter phase 2.
6. Loop until `requirements_locked`, then **immediately** enter phase 2.

## Phase 2 — deep research

1. **Analyze & route** usable modules (GitHub need → `github` REST/code preferred).
2. **Breadth** = investigation directions; **depth** = layers along one direction.
3. Precision is **one knob** (breadth + depth together). Lower tiers trade cost for speed; **Maximum** is different in kind:
   - Quick ≤ 3 layers / 4 directions · Normal ≤ 5 / 6 · Deep ≤ 10 / 8
   - **Maximum**: ignore cost & time; unlimited depth and directions; keep researching until accuracy is enough. Never repeat the same search — deepen or synthesize.
4. AI builds a **project knowledge association graph**; only `valid=true` knowledge is stored.
5. **Stage memory** is a radiating tree (branch side-paths for later follow-ups). The engine does **not** dump full memory bodies into the model context.
6. Every deep-research turn **must obtain** both directories: `memory_catalog` + `knowledge_graph_catalog`. Opening bodies is the model's choice, constrained by precision:
   - **Quick**: current task or shallow related memory on the current branch only
   - **Normal**: related dependency memories and/or full chain on the current branch
   - **Deep**: full current-chain memory + other branch directories, open bodies on demand
   - **Maximum**: no read limits and no cost limits — push reading all related memories and use the knowledge graph frequently; keep deepening until the report would be accurate
7. MCP: knowledge `get/add/update/delete/link/catalog`; memory `catalog/get/chain/add/branch_*`.
8. Mid-research ask_user/choices allowed; ends with `synthesize` + `final`.

## XAIOP (`meta.kind = research_run`)

| phase | Purpose |
|-------|---------|
| `start` | Run created |
| `thinking` | Model reasoning |
| `keyword` / `plan` / `searching` | Plan / keywords |
| `evidence` | Hits / knowledge nodes |
| `clarify` / `directions` | Ask user (phase 1 or 2) |
| `confirm_need` | Model proposed clear need — wait for user Confirm |
| `requirements_locked` | Need confirmed → deep research starts |
| `next_step` | `stage=research`, `status=running` |
| `synthesize` | Markdown report |
| `final` | Complete (`stage=research`) |
| `cancelled` / `error` | Terminal |

## C API

```c
xscope_research_start / continue / cancel / poll_xaiop / status / evidence_list
```

## Related

- [Streaming & XAIOP](streaming-xaiop.md)
- [Prompts & MCP](prompts-mcp.md)
- [Bocha](bocha.md)
