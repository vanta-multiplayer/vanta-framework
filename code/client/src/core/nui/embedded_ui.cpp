#include <utils/safe_win32.h>

#include "embedded_ui.h"

#include "core/nui/page_events.h"
#include "generated/ui_content.hpp"

#include <gui/manager.h>
#include <gui/resources/memory_provider.h>
#include <gui/resources/resource_handler.h>
#include <gui/resources/resource_path.h>
#include <logging/logger.h>

#include "include/cef_parser.h"

#include <memory>
#include <string>

namespace VantaMP::Nui {
    namespace {
        std::string HtmlAsString() {
            return {reinterpret_cast<const char *>(VantaMP::UI::Generated::kHtml), VantaMP::UI::Generated::kHtmlSize};
        }
    } // namespace

    void RegisterEmbeddedInterface(Framework::GUI::Manager &manager) {
        auto pages = std::make_shared<Framework::GUI::Resources::MemoryProvider>("vanta-ui");
        const auto html = HtmlAsString();

        pages->Set("ui.html", html, "text/html");
        pages->Set("index.html", html, "text/html");

        manager.RegisterResourceRoot("vanta", pages);

        manager.RegisterSchemeHandlerFactory("http", "vanta", [pages](CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &, CefRefPtr<CefRequest> request) {
            CefURLParts parts;

            if (!CefParseURL(request->GetURL(), parts)) {
                return CefRefPtr<CefResourceHandler>(new Framework::GUI::Resources::ResourceHandler(400, "400 Bad Request", "vanta", ""));
            }

            std::string path;

            if (!Framework::GUI::Resources::NormalizeResourcePath(CefString(&parts.path).ToString(), path)) {
                return CefRefPtr<CefResourceHandler>(new Framework::GUI::Resources::ResourceHandler(403, "403 Forbidden", "vanta", ""));
            }

            if (path.empty() || path == "index.html") {
                path = "ui.html";
            }

            if (auto events = TryHandlePageEvent(request, path)) {
                return events;
            }

            return CefRefPtr<CefResourceHandler>(new Framework::GUI::Resources::ResourceHandler(pages, "vanta", path));
        });

        Framework::Logging::GetLogger("NUI")->info("Serving the interface from the DLL at {}", kEmbeddedUiUrl);
    }
} // namespace VantaMP::Nui
