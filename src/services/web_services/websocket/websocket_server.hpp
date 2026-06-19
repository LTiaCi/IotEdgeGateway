#pragma once

#include "mongoose.h"

#include <functional>
#include <string>

namespace iotgw {
namespace services {
namespace web_services {
namespace websocket {

class MongooseServer {
 public:
  struct Options {
    std::string listen_addr = "http://0.0.0.0:8080";
    std::string ws_path = "/ws";
    std::string www_root = "www";
    std::string stream_www_root = "/mnt/www";
    std::string stream_root = "/mnt/www/stream";
    std::string media_root = "/userdata/www";
  };

  using HttpHandler = std::function<bool(const std::string& method,
                                         const std::string& uri,
                                         const std::string& body,
                                         std::string& content_type,
                                         int& status,
                                         std::string& response)>;
  using WsMessageHandler =
      std::function<void(const std::string& text, MongooseServer& server)>;

  MongooseServer() { mg_mgr_init(&mgr_); }
  ~MongooseServer() { mg_mgr_free(&mgr_); }

  bool Start(const Options& options) {
    options_ = options;
    listener_ = mg_http_listen(&mgr_, options_.listen_addr.c_str(),
                               &MongooseServer::StaticHandler, this);
    return listener_ != nullptr;
  }

  void Poll(int timeout_ms) { mg_mgr_poll(&mgr_, timeout_ms); }

  struct mg_mgr* GetMgr() { return &mgr_; }

  void SetHttpHandler(HttpHandler handler) { http_handler_ = std::move(handler); }

  void SetWsMessageHandler(WsMessageHandler handler) {
    ws_message_handler_ = std::move(handler);
  }

  void BroadcastText(const std::string& text) {
    for (struct mg_connection* c = mgr_.conns; c != nullptr; c = c->next) {
      if (c->is_websocket) {
        mg_ws_send(c, text.c_str(), text.size(), WEBSOCKET_OP_TEXT);
      }
    }
  }

 private:
  static const char* MgStrData(const struct mg_str& s) { return s.buf; }

  static void StaticHandler(struct mg_connection* c, int ev, void* ev_data) {
    auto* self = static_cast<MongooseServer*>(c->fn_data);
    if (self) {
      self->Handle(c, ev, ev_data);
    }
  }

  bool IsWsPath(const struct mg_http_message* hm) const {
    return hm && mg_match(hm->uri, mg_str(options_.ws_path.c_str()), nullptr);
  }

  void Handle(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
      auto* hm = static_cast<struct mg_http_message*>(ev_data);
      if (IsWsPath(hm)) {
        mg_ws_upgrade(c, hm, nullptr);
        return;
      }

      std::string method(MgStrData(hm->method), hm->method.len);
      std::string uri(MgStrData(hm->uri), hm->uri.len);
      std::string body(MgStrData(hm->body), hm->body.len);
      std::string content_type = "application/json";
      std::string response;
      int status = 404;
      if (http_handler_ &&
          http_handler_(method, uri, body, content_type, status, response)) {
        const std::string headers =
            "Content-Type: " + content_type +
            "\r\nAccess-Control-Allow-Origin: *"
            "\r\nCache-Control: no-store, no-cache, must-revalidate"
            "\r\nPragma: no-cache"
            "\r\nExpires: 0\r\n";
        if (content_type == "image/jpeg" || content_type == "video/mp4" ||
            content_type == "application/octet-stream") {
          mg_http_reply(c, status, headers.c_str(), "%.*s",
                        static_cast<int>(response.size()), response.data());
        } else {
          mg_http_reply(c, status, headers.c_str(), "%s", response.c_str());
        }
        return;
      }

      if (uri.compare(0, std::string("/stream/").size(), "/stream/") == 0) {
        struct mg_http_serve_opts opts = {};
        opts.root_dir = options_.stream_www_root.c_str();
        opts.extra_headers = "Cache-Control: no-cache\r\n";
        opts.mime_types = ".m3u8=application/vnd.apple.mpegurl,.ts=video/mp2t";
        mg_http_serve_dir(c, hm, &opts);
        return;
      }

      if (uri.compare(0, std::string("/media/").size(), "/media/") == 0) {
        struct mg_http_serve_opts opts = {};
        const std::string media_mapping = "/media=" + options_.media_root + "/media";
        opts.root_dir = media_mapping.c_str();
        opts.extra_headers = "Cache-Control: no-cache\r\n";
        opts.mime_types = ".mp4=video/mp4,.jpg=image/jpeg";
        mg_http_serve_dir(c, hm, &opts);
        return;
      }

      if (uri == "/snapshot.jpg" || uri == "/record.mp4") {
        struct mg_http_serve_opts opts = {};
        opts.root_dir = options_.media_root.c_str();
        opts.extra_headers = "Cache-Control: no-cache\r\n";
        opts.mime_types = ".mp4=video/mp4,.jpg=image/jpeg";
        mg_http_serve_dir(c, hm, &opts);
        return;
      }

      struct mg_http_serve_opts opts = {};
      opts.root_dir = options_.www_root.c_str();
      mg_http_serve_dir(c, hm, &opts);
    } else if (ev == MG_EV_WS_MSG) {
      auto* wm = static_cast<struct mg_ws_message*>(ev_data);
      if (wm && ws_message_handler_) {
        ws_message_handler_(std::string(MgStrData(wm->data), wm->data.len), *this);
      }
    }
  }

  struct mg_mgr mgr_;
  struct mg_connection* listener_ = nullptr;
  Options options_;
  HttpHandler http_handler_;
  WsMessageHandler ws_message_handler_;
};

}  // namespace websocket
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
