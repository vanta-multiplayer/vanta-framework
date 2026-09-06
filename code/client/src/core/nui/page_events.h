#pragma once

#include "include/cef_resource_handler.h"

#include <functional>
#include <string>

namespace VantaMP::Nui {
    using FromPage = std::function<void(const std::string &payload)>;

    // Core listens for POSTs to http://vanta/emit.
    void On(const std::string &event, FromPage handler);
    void ClearHandlers();

    // Core -> page. The page library pulls these from http://vanta/events.
    void PushToPage(const std::string &event, const std::string &argsJson);

    CefRefPtr<CefResourceHandler> TryHandlePageEvent(CefRefPtr<CefRequest> request, const std::string &path);
} // namespace VantaMP::Nui
