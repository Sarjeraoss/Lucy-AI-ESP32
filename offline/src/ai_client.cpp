// AI client for Lucy Offline OS.
//
// Two transports, one API:
//   * LAN:   http://<pc>:11434            (Ollama directly, no auth)
//   * Cloud: https://lucy.blueflutex.in   (token-protected gateway -> Ollama)
//
// Both expose /api/chat and /api/tags, so the request/response code below is
// identical; only the base URL, the token header and TLS differ.
//
// Lucy remains offline-first: every failure returns nullptr quickly so the
// caller falls back to the built-in keyword/intent/Markov brain.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <ArduinoJson.h>

#include "ai_api.h"
#include "tls_root_ca.h"
#include "lucy_cloud_config.h"

namespace {

constexpr uint16_t DEFAULT_PORT = 11434;
constexpr size_t AI_OUT_MAX = 768;
constexpr size_t AI_USER_MAX = 160;
constexpr size_t AI_BOT_MAX = 512;
constexpr int AI_HISTORY = 3;
constexpr unsigned long RETRY_BACKOFF_MS = 30000;
constexpr unsigned long LAN_TIMEOUT_MS = 15000;
constexpr unsigned long CLOUD_TIMEOUT_MS = 26000;

#if defined(LUCY_CLIPBOT_C3)
char baseUrl[96] = LUCY_WORKER_URL;
#else
char baseUrl[96] = "http://192.168.4.2:11434";
#endif
char modelCfg[40] = "qwen2.5:7b";
std::atomic<uint8_t> replyLanguage{0};
char tokenCfg[96] = "";
QueueHandle_t completed = nullptr;
bool chatBusy = false;
char pendingText[AI_USER_MAX];

unsigned long retryAt = 0;
std::atomic<int> lastStatus{0};
char outBuf[AI_OUT_MAX];
char lastError[96] = "";

// Rolling history so the model can follow a short conversation.
char histUser[AI_HISTORY][AI_USER_MAX];
char histBot[AI_HISTORY][AI_BOT_MAX];
int histCount = 0;

const char SYSTEM_PROMPT[] =
    "You are Lucy, a warm, playful companion living on a tiny offline ESP32 board. "
    "Reply in 1 to 3 short sentences, under 40 words. "
    "Write in plain Latin characters only (ASCII). If the user writes Hindi or Marathi, "
    "answer in romanized Latin letters. Never output Chinese, Devanagari, or other scripts. "
    "Never claim internet access, browsing, or live data. Be concrete, kind, and a little curious.";

bool usesTls() { return strncmp(baseUrl, "https://", 8) == 0; }

// True when the configured transport can plausibly work right now.
bool networkReady() {
  if (usesTls()) return WiFi.status() == WL_CONNECTED;      // cloud needs STA
  return WiFi.softAPgetStationNum() > 0 || WiFi.status() == WL_CONNECTED;
}

String endpoint(const char *path) {
  if (baseUrl[0] == '\0') return String(path);
  return String(baseUrl) + path;
}

void setError(const char *msg) {
  if (!msg) msg = "";
  strncpy(lastError, msg, sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
}

void appendEscaped(String &out, const char *text) {
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (*p < 0x20) out += ' ';
        else out += (char)*p;
        break;
    }
  }
}

void pushHistory(const char *user, const char *bot) {
  if (histCount == AI_HISTORY) {
    memmove(histUser[0], histUser[1], sizeof(histUser[0]) * (AI_HISTORY - 1));
    memmove(histBot[0], histBot[1], sizeof(histBot[0]) * (AI_HISTORY - 1));
    histCount = AI_HISTORY - 1;
  }
  strncpy(histUser[histCount], user, AI_USER_MAX - 1);
  histUser[histCount][AI_USER_MAX - 1] = '\0';
  strncpy(histBot[histCount], bot, AI_BOT_MAX - 1);
  histBot[histCount][AI_BOT_MAX - 1] = '\0';
  histCount++;
}

// Shared HTTP setup so chat and ping behave the same.
bool beginRequest(HTTPClient &http, WiFiClient &plain, WiFiClientSecure &secure,
                  const String &url) {
  if (usesTls()) {
    secure.setCACert(LUCY_ROOT_CA);
    secure.setHandshakeTimeout(10);
    if (!http.begin(secure, url)) return false;
    http.setTimeout(CLOUD_TIMEOUT_MS);
    http.setConnectTimeout(10000);
  } else {
    if (!http.begin(plain, url)) return false;
    http.setTimeout(LAN_TIMEOUT_MS);
    http.setConnectTimeout(3000);
  }
  http.setReuse(false);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  return true;
}

void addAuth(HTTPClient &http) {
#if defined(LUCY_CLIPBOT_C3)
  if (tokenCfg[0] != '\0') http.addHeader("x-lucy-token", tokenCfg);
#else
  if (tokenCfg[0] != '\0') http.addHeader("x-lucy-token", tokenCfg);
#endif
}

bool requestChat(const char *text) {
  if (!networkReady() || (usesTls() && time(nullptr) < 1704067200)) {
    lastStatus = 0;
    setError("network or TLS clock not ready");
    Serial.println("AI chat: network or TLS clock not ready");
    return false;
  }
  String body;
  body.reserve(1024);
#if defined(LUCY_CLIPBOT_C3)
  body += "{\"stream\":false,\"messages\":[";
  for (int i = 0; i < histCount; ++i) {
    if (i) body += ",";
    body += "{\"role\":\"user\",\"content\":\"";
    appendEscaped(body, histUser[i]);
    body += "\"},{\"role\":\"assistant\",\"content\":\"";
    appendEscaped(body, histBot[i]);
    body += "\"}";
  }
  if (histCount) body += ",";
  body += "{\"role\":\"user\",\"content\":\"";
  appendEscaped(body, text);
  body += "\"}]}";
#else
  body += "{\"model\":\"";
  appendEscaped(body, modelCfg);
  body += "\",\"stream\":false,\"keep_alive\":\"10m\",\"messages\":[{\"role\":\"system\",\"content\":\"";
  appendEscaped(body, SYSTEM_PROMPT);
  appendEscaped(body, replyLanguage == 1 ? " Always reply in romanized Hindi." :
      (replyLanguage == 2 ? " Always reply in romanized Marathi." : " Always reply in English."));
  body += "\"}";
  for (int i = 0; i < histCount; i++) {
    body += ",{\"role\":\"user\",\"content\":\"";
    appendEscaped(body, histUser[i]);
    body += "\"},{\"role\":\"assistant\",\"content\":\"";
    appendEscaped(body, histBot[i]);
    body += "\"}";
  }
  body += ",{\"role\":\"user\",\"content\":\"";
  appendEscaped(body, text);
  body += "\"}],\"options\":{\"temperature\":0.7,\"num_predict\":90}}";
#endif

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  if (!beginRequest(http, plain, secure, endpoint("/api/chat"))) {
    lastStatus = 0;
    setError("HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  addAuth(http);
  Serial.println("AI chat: request started");
  lastStatus = http.POST((uint8_t *)body.c_str(), body.length());
  Serial.printf("AI chat: HTTP %d\n", lastStatus.load());
  if (lastStatus != 200) {
    String errBody;
    errBody.reserve(160);
    WiFiClient *s = http.getStreamPtr();
    unsigned long t0 = millis();
    while (s && s->connected() && !s->available() && millis() - t0 < 300) delay(1);
    while (s && s->available() && errBody.length() < 150) {
      char c = (char)s->read();
      if (c >= 32 || c == ' ') errBody += c;
    }
    http.end();
    char msg[96];
    if (errBody.length()) snprintf(msg, sizeof(msg), "HTTP %d: %.60s", lastStatus.load(), errBody.c_str());
    else snprintf(msg, sizeof(msg), "HTTP %d", lastStatus.load());
    setError(msg);
    return false;
  }
  String raw;
  raw.reserve(1536);
  WiFiClient *stream = http.getStreamPtr();
  unsigned long t0 = millis();
  while (stream && (stream->connected() || stream->available()) && raw.length() < 1500 && millis() - t0 < 3000) {
    while (stream->available() && raw.length() < 1500) {
      char c = (char)stream->read();
      raw += c;
      t0 = millis();
    }
    if (!stream->available()) delay(5);
  }
  http.end();
  String respBody = raw;
  if (respBody.startsWith("HTTP/1")) {
    int hdrEnd = respBody.indexOf("\r\n\r\n");
    if (hdrEnd >= 0) respBody = respBody.substring(hdrEnd + 4);
  }
  // Defensive: tolerate chunked transfer encoding if a proxy/server sends it.
  if (respBody.indexOf("\r\n") > 0 && respBody.indexOf("\r\n") < 6) {
    bool hexish = true;
    int nl = respBody.indexOf("\r\n");
    for (int i = 0; i < nl; ++i) { char c = respBody[i]; if (!isxdigit((unsigned char)c)) { hexish = false; break; } }
    if (hexish) {
      String dec; dec.reserve(respBody.length());
      int pos = 0;
      while (pos < (int)respBody.length()) {
        int lineEnd = respBody.indexOf("\r\n", pos);
        if (lineEnd < 0) break;
        String lenHex = respBody.substring(pos, lineEnd);
        long chunkLen = strtol(lenHex.c_str(), nullptr, 16);
        pos = lineEnd + 2;
        if (chunkLen <= 0) break;
        int avail = min((long)(respBody.length() - pos), chunkLen);
        dec += respBody.substring(pos, pos + avail);
        pos += avail;
        if (respBody.startsWith("\r\n", pos)) pos += 2;
      }
      if (dec.length()) respBody = dec;
    }
  }
  Serial.printf("AI chat: body length=%u\n", (unsigned)respBody.length());
  if (respBody.length() > 0) {
    String dbg = respBody.substring(0, 120);
    dbg.replace("\n", "\\n"); dbg.replace("\r", "\\r");
    Serial.printf("AI chat: body head=%s\n", dbg.c_str());
  }
  DynamicJsonDocument response(2048);
  const DeserializationError err = deserializeJson(response, respBody);
  if (err) {
    char msg[96];
    snprintf(msg, sizeof(msg), "JSON parse: %s", err.c_str());
    setError(msg);
    Serial.printf("AI chat: %s\n", msg);
    return false;
  }
  const char *content = response["message"]["content"].as<const char*>();
  if (!content || !content[0]) content = response["reply"].as<const char*>();
  if (!content || !content[0]) content = response["response"].as<const char*>();
  if (!content || !content[0]) content = response["text"].as<const char*>();
  if (!content || !content[0]) {
    setError("empty AI content");
    Serial.printf("AI chat: message.content missing or empty msgNull=%d\n", response["message"].isNull()?1:0);
    return false;
  }
  strncpy(outBuf, content, sizeof(outBuf) - 1);
  outBuf[sizeof(outBuf) - 1] = '\0';
  pushHistory(text, outBuf);
  setError("");
  Serial.println("AI chat: response received");
  return true;
}

} // namespace

namespace {
struct ChatResult {
  bool ok;
  char text[AI_USER_MAX];
  char error[96];
};

void chatTask(void *arg) {
  char *text = static_cast<char *>(arg);
  ChatResult res = {};
  res.ok = requestChat(text);
  strncpy(res.text, text, sizeof(res.text) - 1);
  strncpy(res.error, lastError, sizeof(res.error) - 1);
  xQueueSend(completed, &res, portMAX_DELAY);
  free(text);
  vTaskDelete(nullptr);
}

bool ensureQueue() {
  if (!completed) completed = xQueueCreate(1, sizeof(ChatResult));
  return completed != nullptr;
}
}

void aiSetUrl(const char *url) {
  if (chatBusy) return;
  if (url != nullptr && url[0] != '\0') {
    strncpy(baseUrl, url, sizeof(baseUrl) - 1);
    baseUrl[sizeof(baseUrl) - 1] = '\0';
    // Strip trailing slashes so endpoint() stays simple.
    int len = strlen(baseUrl);
    while (len > 0 && baseUrl[len - 1] == '/') baseUrl[--len] = '\0';
  }
  retryAt = 0;
}

void aiSetEndpoint(const char *host, uint16_t port) {
  if (host == nullptr || host[0] == '\0') return;
  if (strncmp(host, "http://", 7) == 0 || strncmp(host, "https://", 8) == 0) {
    aiSetUrl(host);
    return;
  }
  char url[96];
  snprintf(url, sizeof(url), "http://%s:%u", host, port == 0 ? DEFAULT_PORT : port);
  aiSetUrl(url);
}

void aiSetModel(const char *model) {
  if (chatBusy) return;
  if (model != nullptr && model[0] != '\0') {
    strncpy(modelCfg, model, sizeof(modelCfg) - 1);
    modelCfg[sizeof(modelCfg) - 1] = '\0';
  }
  retryAt = 0;
}
void aiSetLanguage(uint8_t language) { replyLanguage = language <= 2 ? language : 0; }

void aiSetToken(const char *token) {
  if (chatBusy) return;
  if (token == nullptr) return;
  strncpy(tokenCfg, token, sizeof(tokenCfg) - 1);
  tokenCfg[sizeof(tokenCfg) - 1] = '\0';
  retryAt = 0;
}

void aiSetInsecureTls(bool insecure) {
  (void)insecure;
  retryAt = 0;
}

const char *aiUrl() { return baseUrl; }
const char *aiModel() { return modelCfg; }
const char *aiToken() { return tokenCfg; }
bool aiInsecure() { return false; }
size_t aiTokenLength() { return strlen(tokenCfg); }
bool aiUsesTls() { return usesTls(); }
int aiLastStatus() { return lastStatus; }
const char *aiLastError() { return lastError; }

void aiClearRetry() { retryAt = 0; setError(""); }
void aiReset() {
  retryAt = 0;
  setError("");
  lastStatus = 0;
  chatBusy = false;
  pendingText[0] = '\0';
  if (completed) xQueueReset(completed);
}
void aiSetIdleHook(void (*hook)()) { (void)hook; }

AiRequestResult aiRequestChat(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    setError("empty request");
    return AI_REQ_BAD_INPUT;
  }
  if (chatBusy) {
    setError("busy");
    return AI_REQ_BUSY;
  }
  if (retryAt != 0 && int32_t(millis() - retryAt) < 0) {
    setError("backoff");
    return AI_REQ_BACKOFF;
  }
  if (!networkReady()) {
    lastStatus = 0;
    setError("network unavailable");
    retryAt = millis() + RETRY_BACKOFF_MS;
    return AI_REQ_NETWORK;
  }
  if (!ensureQueue()) {
    setError("queue allocation failed");
    retryAt = millis() + RETRY_BACKOFF_MS;
    return AI_REQ_NOMEM;
  }
  if (ESP.getFreeHeap() < 80000) {
    setError("insufficient memory");
    Serial.printf("AI chat: insufficient memory (%u bytes free)\n", ESP.getFreeHeap());
    retryAt = millis() + RETRY_BACKOFF_MS;
    return AI_REQ_NOMEM;
  }
  char *copy = static_cast<char *>(malloc(AI_USER_MAX));
  if (!copy) {
    setError("buffer allocation failed");
    retryAt = millis() + RETRY_BACKOFF_MS;
    return AI_REQ_NOMEM;
  }
  strncpy(copy, text, AI_USER_MAX - 1);
  copy[AI_USER_MAX - 1] = '\0';
  strncpy(pendingText, copy, sizeof(pendingText) - 1);
  pendingText[sizeof(pendingText) - 1] = '\0';
  chatBusy = true;
  setError("");
  if (xTaskCreate(chatTask, "lucy-chat", 12288, copy, 1, nullptr) != pdPASS) {
    chatBusy = false;
    free(copy);
    setError("task allocation failed");
    Serial.println("AI chat: task allocation failed");
    retryAt = millis() + RETRY_BACKOFF_MS;
    return AI_REQ_TASK_FAILED;
  }
  Serial.println("AI chat: task started");
  return AI_REQ_STARTED;
}

const char *aiReply(const char *text) {
  (void)aiRequestChat(text);
  return nullptr; // async; poll aiPollResult/aiPollReply
}

bool aiPing(unsigned long timeoutMs) {
  if (chatBusy) return false;
  if (!networkReady()) {
    lastStatus = 0;
    return false;
  }

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
#if defined(LUCY_CLIPBOT_C3)
  String url = endpoint("/api/tags");
#else
  String url = endpoint("/api/tags");
#endif
  if (usesTls()) {
    secure.setCACert(LUCY_ROOT_CA);
    secure.setHandshakeTimeout(10);
    if (!http.begin(secure, url)) return false;
  } else {
    if (!http.begin(plain, url)) return false;
  }
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setReuse(false);

  addAuth(http);
  lastStatus = http.GET();
  http.end();
  return lastStatus == 200;
}

AiReplyStatus aiPollResult(const char **text, const char **reply) {
  if (reply) *reply = nullptr;
  if (text) *text = nullptr;
  if (!chatBusy || !completed) return AI_REPLY_NONE;
  ChatResult res = {};
  if (xQueueReceive(completed, &res, 0) != pdTRUE) return AI_REPLY_NONE;
  chatBusy = false;
  strncpy(pendingText, res.text, sizeof(pendingText) - 1);
  pendingText[sizeof(pendingText) - 1] = '\0';
  if (text) *text = pendingText;
  if (res.ok) {
    retryAt = 0;
    setError("");
    if (reply) *reply = outBuf;
    return AI_REPLY_OK;
  }
  retryAt = millis() + RETRY_BACKOFF_MS;
  strncpy(lastError, res.error[0] ? res.error : "AI request failed", sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
  return AI_REPLY_ERROR;
}

// Legacy wrapper.
const char *aiPollReply(const char **text) {
  const char *reply = nullptr;
  AiReplyStatus st = aiPollResult(text, &reply);
  if (st == AI_REPLY_OK) return reply;
  return nullptr;
}
bool aiBusy() { return chatBusy; }
