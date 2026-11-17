#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "cat_animation.h"

// A deliberately small, stateless MCP server (2025-06-18), read-only tools.
// HTTP authentication, Origin validation, body/rate limits live in main.cpp.
namespace lucy_mcp {
inline void error(JsonDocument &out, int code, const char *message) {
  out.remove("result");
  out["error"]["code"] = code;
  out["error"]["message"] = message;
}
inline int respond(JsonVariantConst in, JsonDocument &out, const char *status) {
  out["jsonrpc"] = "2.0";
  out["id"] = in["id"];
  if (!in.is<JsonObjectConst>() || strcmp(in["jsonrpc"] | "", "2.0") ||
      !in["method"].is<const char *>() ||
      (in.containsKey("id") && !(in["id"].is<const char *>() || in["id"].is<long>()))) {
    out["id"] = nullptr;
    error(out, -32600, "Invalid Request");
    return 200;
  }
  const char *method = in["method"];
  if (!in.containsKey("id")) return 202;
  if (strcmp(method, "initialize") == 0) {
    const char *requested = in["params"]["protocolVersion"] | "";
    out["result"]["protocolVersion"] = strcmp(requested, "2025-03-26") == 0 ? "2025-03-26" : "2025-06-18";
    out["result"]["capabilities"]["tools"]["listChanged"] = false;
    out["result"]["serverInfo"]["name"] = "lucy-companion";
    out["result"]["serverInfo"]["version"] = "0.3.0";
  } else if (strcmp(method, "ping") == 0) {
    out.createNestedObject("result");
  } else if (strcmp(method, "tools/list") == 0) {
    auto tools = out["result"].createNestedArray("tools");
    for (int i = 0; i < 2; i++) {
      auto tool = tools.createNestedObject();
      tool["name"] = i == 0 ? "lucy_status" : "lucy_animations";
      tool["description"] = i == 0 ? "Read Lucy uptime, memory, battery and network status" : "List Lucy's 24 behaviours and five variants (120 clips)";
      tool["inputSchema"]["type"] = "object";
      tool["inputSchema"].createNestedObject("properties");
      tool["inputSchema"]["additionalProperties"] = false;
      tool["annotations"]["readOnlyHint"] = true;
    }
  } else if (strcmp(method, "tools/call") == 0) {
    const char *tool = in["params"]["name"] | "";
    auto args = in["params"]["arguments"];
    if ((!args.isNull() && (!args.is<JsonObjectConst>() || args.size() != 0)) ||
        (strcmp(tool, "lucy_status") && strcmp(tool, "lucy_animations"))) {
      error(out, -32602, "Unknown tool or invalid arguments");
    } else {
      auto content = out["result"].createNestedArray("content").createNestedObject();
      content["type"] = "text";
      if (strcmp(tool, "lucy_status") == 0) content["text"] = status;
      else {
        // Copy serialized JSON as MCP text content; no large dynamic catalog.
        char catalog[1100] = "120 clips. Each behaviour has Gentle, Bouncy, Curious, Dreamy and Spirited variants: ";
        for (int i = 0; i < cat::BEHAVIOURS; i++) {
          strncat(catalog, cat::NAMES[i], sizeof(catalog) - strlen(catalog) - 1);
          strncat(catalog, i + 1 == cat::BEHAVIOURS ? "." : ", ", sizeof(catalog) - strlen(catalog) - 1);
        }
        content["text"] = catalog; // mutable char array: ArduinoJson copies it
      }
      out["result"]["isError"] = false;
    }
  } else error(out, -32601, "Method not found");
  return 200;
}
} // namespace lucy_mcp
