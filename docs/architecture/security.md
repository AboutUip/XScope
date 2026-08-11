# Security & secrets

[中文](security.zh-CN.md)

## Goals (pragmatic)

Protect against common risks:

- Database file copied off the machine should not expose plaintext research data or API keys
- Casual access by other apps should not trivially read secrets

Out of scope for v1 extremes: anonymous threat actors with full live malware in the user session, custom anti-cheat style hardening, etc.

There is **no login**. Security must work without accounts.

## Secret types

| Secret | Scope | Storage |
|--------|-------|---------|
| Many search-engine / extension **API keys** | **Global** (all projects) | Encrypted global DB (or equivalent global secret store backed by the same crypto) |
| Project research content | Per project | Encrypted project DB + files under project dir |

## Encryption approach (v1 implemented)

1. **Master key** (32 bytes) stored beside `global.db`, wrapped with **Windows DPAPI**.
2. **Secrets / API keys** in `global.db` sealed with **AES-256-GCM** under that master key.
3. DB files live only under the client-provided private `data_root`.
4. Full SQLCipher-class page encryption remains an optional upgrade behind `storage::Database`.

Platform ports later: macOS Keychain, Android Keystore, etc.

Trade-off (accepted for local, no-login products): reinstalling the OS profile, moving to another machine, or clearing key material may require re-entering API keys unless a future backup/export feature is added.

## API keys

- Stored **globally**, not per project
- Always written/read through SDK secret APIs — never logged in plaintext
- Expect **many** keys; the schema/API should allow a list/map of provider → secret, not a single hard-coded field

## Client vs SDK

- Client: pick private `data_root`, never print secrets in UI logs by default
- SDK: encrypt-at-rest, parameterized SQL, least exposure of key bytes in memory APIs

## Minimal v1

Ship encryption + key protection plumbing with a tiny schema. Do not invent a full threat model document before features exist; extend controls as secret-bearing features land.
