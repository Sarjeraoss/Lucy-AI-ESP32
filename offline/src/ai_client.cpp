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
#include <ArduinoJson.h>

#include "ai_api.h"
#include "tls_root_ca.h"

namespace {

constexpr uint16_t DEFAULT_PORT = 11434;
constexpr size_t AI_OUT_MAX = 768;
constexpr size_t AI_USER_MAX = 160;
constexpr size_t AI_BOT_MAX = 512;
constexpr int AI_HISTORY = 3;
constexpr unsigned long RETRY_BACKOFF_MS = 30000;
constexpr unsigned long LAN_TIMEOUT_MS = 15000;
constexpr unsigned long CLOUD_TIMEOUT_MS = 45000;

char baseUrl[96] = "http://192.168.4.2:11434";
char modelCfg[40] = "qwen2.5:7b";
uint8_t replyLanguage = 0;
char tokenCfg[96] = "";
bool insecureTls = false;

unsigned long retryAt = 0;
int lastStatus = 0;
void (*idleHook)() = nullptr;
char outBuf[AI_OUT_MAX];

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
    if (insecureTls) {
      secure.setInsecure();
    } else {
      secure.setCACert(LUCY_ROOT_CA);
    }
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
  if (tokenCfg[0] != '\0') http.addHeader("x-lucy-token", tokenCfg);
}

const char *requestChat(const char *text) {
  if (!networkReady()) return nullptr;

  String body;
  body.reserve(1024);
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

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  if (!beginRequest(http, plain, secure, endpoint("/api/chat"))) return nullptr;

  http.addHeader("Content-Type", "application/json");
  addAuth(http);
  lastStatus = http.POST((uint8_t *)body.c_str(), body.length());
  if (lastStatus != 200) {
    http.end();
    return nullptr;
  }
  // Parse only message.content, with a fixed-capacity document and nesting cap.
  // Reading the stream avoids copying an unbounded server response into a String.
  StaticJsonDocument<128> filter;
  filter["message"]["content"] = true;
  DynamicJsonDocument response(2048);
  // HTTP/1.0 avoids chunked transfer framing on the stream.
  const auto err = deserializeJson(response, http.getStream(),
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
  http.end();
  if (err || !response["message"]["content"].is<const char *>()) return nullptr;
  const char *content = response["message"]["content"];
  if (!content[0]) return nullptr;
  strncpy(outBuf, content, sizeof(outBuf) - 1);
  outBuf[sizeof(outBuf) - 1] = '\0';
  pushHistory(text, outBuf);
  return outBuf;
}

} // namespace

namespace {
struct ChatJob { const char *text; const char *reply; TaskHandle_t caller; };
void chatTask(void *context) {
  auto *job = static_cast<ChatJob *>(context);
  job->reply = requestChat(job->text);
  xTaskNotifyGive(job->caller);
  vTaskDelete(nullptr);
}
const char *chatWithUi(const char *text) {
  if (!networkReady()) return nullptr;
  ChatJob job{text, nullptr, xTaskGetCurrentTaskHandle()};
  // Leave enough heap for mbedTLS as well as the UI and network stacks.
  if (ESP.getFreeHeap() < 80000 || xTaskCreate(chatTask, "lucy-chat", 12288, &job, 1, nullptr) != pdPASS) return nullptr;
  while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(4)) == 0) {
    if (idleHook) idleHook();
  }
  return job.reply;
}
}

void aiSetUrl(const char *url) {
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
  if (model != nullptr && model[0] != '\0') {
    strncpy(modelCfg, model, sizeof(modelCfg) - 1);
    modelCfg[sizeof(modelCfg) - 1] = '\0';
  }
  retryAt = 0;
}
void aiSetLanguage(uint8_t language) { replyLanguage = language <= 2 ? language : 0; }

void aiSetToken(const char *token) {
  if (token == nullptr) return;
  strncpy(tokenCfg, token, sizeof(tokenCfg) - 1);
  tokenCfg[sizeof(tokenCfg) - 1] = '\0';
  retryAt = 0;
}

void aiSetInsecureTls(bool insecure) {
  insecureTls = insecure;
  retryAt = 0;
}

const char *aiUrl() { return baseUrl; }
const char *aiModel() { return modelCfg; }
const char *aiToken() { return tokenCfg; }
bool aiInsecure() { return insecureTls; }
size_t aiTokenLength() { return strlen(tokenCfg); }
bool aiUsesTls() { return usesTls(); }
int aiLastStatus() { return lastStatus; }

void aiClearRetry() { retryAt = 0; }
void aiSetIdleHook(void (*hook)()) { idleHook = hook; }

const char *aiReply(const char *text) {
  if (text == nullptr || text[0] == '\0') return nullptr;
  if (retryAt != 0 && int32_t(millis() - retryAt) < 0) return nullptr;

  const char *reply = chatWithUi(text);
  if (reply == nullptr) {
    // Do not hammer a sleeping server: pause requests for a while.
    retryAt = millis() + RETRY_BACKOFF_MS;
    return nullptr;
  }
  retryAt = 0;
  return reply;
}

bool aiPing(unsigned long timeoutMs) {
  if (!networkReady()) {
    lastStatus = 0;
    return false;
  }

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  String url = endpoint("/api/tags");
  if (usesTls()) {
    if (insecureTls) secure.setInsecure();
    else secure.setCACert(LUCY_ROOT_CA);
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
