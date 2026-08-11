#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef XSCOPE_CAPI_EXPORTS
#define XSCOPE_CAPI __declspec(dllexport)
#else
#define XSCOPE_CAPI __declspec(dllimport)
#endif
#else
#define XSCOPE_CAPI
#endif

typedef struct xscope_workspace xscope_workspace;

/// Free a string returned by this API (malloc-backed).
XSCOPE_CAPI void xscope_string_free(char* s);

/// Open workspace at UTF-8 data_root. Returns null on failure; detail in xscope_last_error.
XSCOPE_CAPI xscope_workspace* xscope_workspace_open(const char* data_root_utf8);
XSCOPE_CAPI void xscope_workspace_close(xscope_workspace* ws);

/// Copy last error message into buf (UTF-8). Returns length needed excluding NUL.
XSCOPE_CAPI int xscope_last_error(char* buf, int buf_len);

/// JSON helpers — caller must xscope_string_free the result (never null; error as {"ok":false,...}).
XSCOPE_CAPI char* xscope_github_oauth_status(xscope_workspace* ws);
XSCOPE_CAPI char* xscope_github_oauth_start(xscope_workspace* ws, const char* scope_utf8,
                                            int open_browser);
XSCOPE_CAPI char* xscope_github_oauth_poll(xscope_workspace* ws, const char* device_code_utf8);
XSCOPE_CAPI char* xscope_github_oauth_set_pat(xscope_workspace* ws, const char* token_utf8,
                                              const char* scope_utf8);
XSCOPE_CAPI char* xscope_github_oauth_disconnect(xscope_workspace* ws);

/// Encrypted secrets (plaintext never returned by has/list helpers).
XSCOPE_CAPI char* xscope_secret_put(xscope_workspace* ws, const char* id_utf8,
                                    const char* provider_utf8, const char* plaintext_utf8);
XSCOPE_CAPI char* xscope_secret_has(xscope_workspace* ws, const char* id_utf8);
XSCOPE_CAPI char* xscope_secret_remove(xscope_workspace* ws, const char* id_utf8);

/// AI providers (DeepSeek / Kimi): status, API key + model sync, preferred model.
XSCOPE_CAPI char* xscope_ai_provider_status(xscope_workspace* ws);
XSCOPE_CAPI char* xscope_ai_set_api_key(xscope_workspace* ws, const char* provider_id_utf8,
                                        const char* api_key_utf8);
XSCOPE_CAPI char* xscope_ai_refresh_models(xscope_workspace* ws, const char* provider_id_utf8);
XSCOPE_CAPI char* xscope_ai_set_preferred_model(xscope_workspace* ws, const char* provider_id_utf8,
                                                const char* model_id_utf8);
/// Set provider model-side capabilities JSON array, e.g. ["chat","image_input","video_input"].
/// Mirrors onto all models for that provider.
XSCOPE_CAPI char* xscope_ai_set_model_capabilities(xscope_workspace* ws,
                                                   const char* provider_id_utf8,
                                                   const char* capabilities_json_utf8);

/// Network research search modules (registry enable toggles).
XSCOPE_CAPI char* xscope_search_modules_list(xscope_workspace* ws);
XSCOPE_CAPI char* xscope_search_module_set_enabled(xscope_workspace* ws, const char* id_utf8,
                                                   int enabled);
/// Store API key for a Bearer/API-key search module (e.g. bocha → secret bocha.default).
XSCOPE_CAPI char* xscope_search_module_set_api_key(xscope_workspace* ws, const char* id_utf8,
                                                   const char* api_key_utf8);

/// Research projects (sidebar library). Persist only when the client chooses to create.
XSCOPE_CAPI char* xscope_project_list(xscope_workspace* ws);
XSCOPE_CAPI char* xscope_project_create(xscope_workspace* ws, const char* title_utf8);
XSCOPE_CAPI char* xscope_project_rename(xscope_workspace* ws, const char* id_utf8,
                                        const char* title_utf8);
XSCOPE_CAPI char* xscope_project_set_pinned(xscope_workspace* ws, const char* id_utf8, int pinned);
XSCOPE_CAPI char* xscope_project_delete(xscope_workspace* ws, const char* id_utf8);

/// Research orchestrator (engine-enforced precision budgets + XAIOP phase stream).
/// precision: 0=Quick, 1=Normal, 2=Deep, 3=Maximum.
XSCOPE_CAPI char* xscope_research_start(xscope_workspace* ws, const char* project_id_utf8,
                                        const char* query_utf8, const char* model_id_utf8,
                                        int precision);
XSCOPE_CAPI char* xscope_research_continue(xscope_workspace* ws, const char* run_id_utf8,
                                           const char* user_reply_utf8);
XSCOPE_CAPI char* xscope_research_cancel(xscope_workspace* ws, const char* run_id_utf8);
/// Next XAIOP wire for the run (wait up to wait_ms). {"ok":true,"wire":"..."} or empty wire.
XSCOPE_CAPI char* xscope_research_poll_xaiop(xscope_workspace* ws, const char* run_id_utf8,
                                             int wait_ms);
XSCOPE_CAPI char* xscope_research_status(xscope_workspace* ws, const char* run_id_utf8);
XSCOPE_CAPI char* xscope_research_evidence_list(xscope_workspace* ws, const char* project_id_utf8,
                                                const char* run_id_utf8);

/// Project knowledge association graph snapshot (nodes + edges JSON).
XSCOPE_CAPI char* xscope_project_knowledge_graph(xscope_workspace* ws, const char* project_id_utf8);

/// Restore UI for a historical project: latest run + report + events + knowledge graph.
XSCOPE_CAPI char* xscope_project_research_snapshot(xscope_workspace* ws,
                                                   const char* project_id_utf8);

#ifdef __cplusplus
}
#endif
