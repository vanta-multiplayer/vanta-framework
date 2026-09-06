#include <utils/safe_win32.h>

#include "page_events.h"

#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_values.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace VantaMP::Nui {
    namespace {
        struct Pending {
            std::string event;
            std::string args;
        };

        std::mutex gMutex;
        std::vector<Pending> gPending;
        std::unordered_map<std::string, FromPage> gHandlers;

        std::string QuoteJsonString(const std::string &text) {
            auto value = CefValue::Create();
            value->SetString(text);
            return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
        }

        std::string DrainPending() {
            std::vector<Pending> batch;
            {
                std::scoped_lock lock(gMutex);
                batch.swap(gPending);
            }

            std::string json = "[";
            for (size_t i = 0; i < batch.size(); ++i) {
                if (i != 0) {
                    json += ',';
                }
                json += "{\"event\":";
                json += QuoteJsonString(batch[i].event);
                json += ",\"args\":";
                json += batch[i].args.empty() ? "[]" : batch[i].args;
                json += '}';
            }
            json += ']';
            return json;
        }

        std::string ReadPostBody(CefRefPtr<CefRequest> request) {
            auto post = request ? request->GetPostData() : nullptr;
            if (!post) {
                return {};
            }

            CefPostData::ElementVector elements;
            post->GetElements(elements);

            std::string body;
            for (const auto &element : elements) {
                if (!element || element->GetType() != PDE_TYPE_BYTES) {
                    continue;
                }

                const auto size = element->GetBytesCount();
                const auto offset = body.size();
                body.resize(offset + size);
                element->GetBytes(size, body.data() + offset);
            }

            return body;
        }

        class JsonHandler final: public CefResourceHandler {
          private:
            std::string _body;
            size_t _offset = 0;

          public:
            explicit JsonHandler(std::string body): _body(std::move(body)) {}

            bool Open(CefRefPtr<CefRequest> request, bool &handleRequest, CefRefPtr<CefCallback> callback) override {
                (void)request;
                (void)callback;
                handleRequest = true;
                return true;
            }

            void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &responseLength, CefString &redirectUrl) override {
                (void)redirectUrl;
                response->SetStatus(200);
                response->SetMimeType("application/json");
                response->SetHeaderByName("Cache-Control", "no-store", true);
                responseLength = static_cast<int64_t>(_body.size());
            }

            bool Skip(int64_t bytesToSkip, int64_t &bytesSkipped, CefRefPtr<CefResourceSkipCallback> callback) override {
                (void)callback;
                const auto remaining = static_cast<int64_t>(_body.size() - _offset);
                bytesSkipped = bytesToSkip > remaining ? remaining : bytesToSkip;
                _offset += static_cast<size_t>(bytesSkipped);
                return true;
            }

            bool Read(void *dataOut, int bytesToRead, int &bytesRead, CefRefPtr<CefResourceReadCallback> callback) override {
                (void)callback;
                const auto remaining = _body.size() - _offset;
                if (remaining == 0) {
                    bytesRead = 0;
                    return false;
                }

                const auto n = remaining < static_cast<size_t>(bytesToRead) ? remaining : static_cast<size_t>(bytesToRead);
                memcpy(dataOut, _body.data() + _offset, n);
                _offset += n;
                bytesRead = static_cast<int>(n);
                return true;
            }

            void Cancel() override {}

            IMPLEMENT_REFCOUNTING(JsonHandler);
        };

        class DeliverToCoreTask final: public CefTask {
          private:
            std::string _event;
            std::string _payload;

          public:
            DeliverToCoreTask(std::string event, std::string payload): _event(std::move(event)), _payload(std::move(payload)) {}

            void Execute() override {
                FromPage handler;
                {
                    std::scoped_lock lock(gMutex);
                    auto it = gHandlers.find(_event);
                    if (it != gHandlers.end()) {
                        handler = it->second;
                    }
                }

                if (handler) {
                    handler(_payload);
                }
            }

            IMPLEMENT_REFCOUNTING(DeliverToCoreTask);
        };
    } // namespace

    void On(const std::string &event, FromPage handler) {
        std::scoped_lock lock(gMutex);
        gHandlers[event] = std::move(handler);
    }

    void ClearHandlers() {
        std::scoped_lock lock(gMutex);
        gHandlers.clear();
        gPending.clear();
    }

    void PushToPage(const std::string &event, const std::string &argsJson) {
        std::scoped_lock lock(gMutex);
        const auto args = argsJson.empty() ? "[]" : argsJson;

        // High-frequency samples: keep only the newest so a slow page does
        // not drain a backlog of stale FPS or download ticks.
        if (event == "netgraph:update" || event == "connection:progress") {
            for (auto &item : gPending) {
                if (item.event == event) {
                    item.args = args;
                    return;
                }
            }
        }

        gPending.push_back({event, args});
    }

    CefRefPtr<CefResourceHandler> TryHandlePageEvent(CefRefPtr<CefRequest> request, const std::string &path) {
        if (path == "events") {
            return new JsonHandler(DrainPending());
        }

        if (path == "emit") {
            const auto body = ReadPostBody(request);
            auto parsed = CefParseJSON(body, JSON_PARSER_RFC);
            std::string event;
            std::string payload = "[]";

            if (parsed && parsed->GetType() == VTYPE_DICTIONARY) {
                auto dict = parsed->GetDictionary();
                event = dict->GetString("event").ToString();
                if (auto args = dict->GetValue("args"); args && args->GetType() == VTYPE_LIST) {
                    payload = CefWriteJSON(args, JSON_WRITER_DEFAULT).ToString();
                }
            }

            if (!event.empty()) {
                CefPostTask(TID_UI, new DeliverToCoreTask(std::move(event), std::move(payload)));
            }

            return new JsonHandler("true");
        }

        return nullptr;
    }
} // namespace VantaMP::Nui
