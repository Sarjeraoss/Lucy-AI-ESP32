#pragma once

#include <stdint.h>
#include <stddef.h>

// Client for a model server: either Ollama on the local PC
// (http://<pc>:11434) or the token-protected public gateway
// (https://lucy.blueflutex.in).
//
// Both speak the same API (/api/chat, /api/tags), so only the base URL,
// the token and the transport change. Lucy stays offline-first: when the
// server is unreachable, aiReply() returns nullptr and the caller falls back
// to the built-in keyword/intent/Markov brain.

// Base URL without a trailing slash. Accepts http:// and https://.
void aiSetUrl(const char *url);
// Convenience for LAN Ollama: builds http://host:port.
void aiSetEndpoint(const char *host, uint16_t port);
void aiSetModel(const char *model);
void aiSetLanguage(uint8_t language);
// Sent as x-lucy-token. Empty = no header (LAN Ollama ignores it anyway).
void aiSetToken(const char *token);
// Compatibility command; verification always remains enabled.
void aiSetInsecureTls(bool insecure);

const char *aiUrl();
const char *aiModel();
// For NVS persistence only; never print this.
const char *aiToken();
bool aiInsecure();
// Length only, so status output never prints the secret.
size_t aiTokenLength();
bool aiUsesTls();

enum AiRequestResult : uint8_t {
  AI_REQ_STARTED = 0,
  AI_REQ_BUSY,
  AI_REQ_BACKOFF,
  AI_REQ_NETWORK,
  AI_REQ_NOMEM,
  AI_REQ_TASK_FAILED,
  AI_REQ_BAD_INPUT,
};

enum AiReplyStatus : uint8_t {
  AI_REPLY_NONE = 0,
  AI_REPLY_OK,
  AI_REPLY_ERROR,
};

// Sends one chat turn (with a short rolling history) asynchronously.
// Returns AI_REQ_STARTED when a background request was successfully started.
// On genuine start/request failures, applies circuit-breaker backoff.
AiRequestResult aiRequestChat(const char *text);
// Legacy wrapper: returns nullptr; use aiRequestChat()/aiPollResult().
const char *aiReply(const char *text);

// GET /api/tags with the token. true = the server answered 200.
bool aiPing(unsigned long timeoutMs = 8000);

// Last HTTP status seen by a request (0 = no response / transport error).
// Lets `AI` distinguish "unreachable" from "wrong token" (401).
int aiLastStatus();
const char *aiLastError();

// Forget the circuit breaker (e.g. after the user fixes the network).
void aiClearRetry();
// Reset stuck busy state (e.g. after AI off/on or manual recovery).
void aiReset();
// Retained for source compatibility; background requests no longer wait on UI.
void aiSetIdleHook(void (*hook)());

// Poll completed async chat. Returns AI_REPLY_NONE while pending/no result.
// On completion, sets *text (original user text) and, for OK, *reply (AI text).
AiReplyStatus aiPollResult(const char **text, const char **reply);
const char *aiPollReply(const char **text); // legacy: OK reply or nullptr
bool aiBusy();
