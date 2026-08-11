# Research orchestrator

[中文](research.zh-CN.md)

Engine-owned research loop with two stages: **requirements discovery** then **deep research**.

## Boundary

| Layer | Owns |
|-------|------|
| **SDK** | Discovery → lock → deep research (depth layers, knowledge graph, module routing), MCP tools, evidence DB, XAIOP phases. |
| **Client** | Submit query, poll/cancel/continue, center choices + report, right-rail live feed. |

## Phase 1 — requirements discovery

1. User submits initial query.
2. Engine may search Bocha / GitHub **to clarify the need** (not to answer it).
3. Model emits `thinking`, then `search` / `ask_user` / `confirm`.
4. Loop until `requirements_locked`, then **immediately** enter phase 2.

## Phase 2 — deep research

1. **Analyze & route** usable modules (GitHub need → `github` REST/code preferred).
2. **Breadth** = investigation directions; **depth** = layers along one direction.
3. Precision caps **max depth per direction** (not breadth):
   - Quick ≤ 3, Normal ≤ 5, Deep ≤ 10, Maximum = unlimited (must be thorough; GitHub → code detail).
4. AI builds a **project knowledge association graph**; only `valid=true` knowledge is stored.
5. **Stage memory** is a radiating tree (branch side-paths for later follow-ups). The engine does **not** dump full memory bodies into the model context.
6. Every deep-research turn **must obtain** both directories: `memory_catalog` + `knowledge_graph_catalog`. Opening bodies is the model's choice, constrained by precision:
   - **Quick**: current task or shallow related memory on the current branch only
   - **Normal**: related dependency memories and/or full chain on the current branch
   - **Deep**: full current-chain memory + other branch directories, open bodies on demand
   - **Maximum**: no read limits — push reading all related memories and use the knowledge graph frequently
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
