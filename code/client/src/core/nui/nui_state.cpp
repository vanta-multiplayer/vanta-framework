#include "internal.h"

#include "core/application.h"
#include "core/nui/events.h"
#include "core/voice_io.h"

#include <core_modules.h>
#include <gui/manager.h>
#include <gui/view.h>

namespace VantaMP::Nui::Internal {
    int gViewId              = -1;
    bool gVisible            = false;
    bool gMenuOpen           = true;
    bool gConsoleOpen        = false;
    bool gInWorld            = false;
    bool gNetgraphOn         = false;
    bool gLogsAttached       = false;
    bool gReconnectPending   = false;
    float gFps               = 0.0f;
    std::string gReconnectTo;

    Framework::GUI::View *FindView() {
        auto *manager = Framework::CoreModules::GetWebManager();

        if (!manager || gViewId < 0) {
            return nullptr;
        }

        for (auto *view : manager->GetAllViews()) {
            if (view && view->GetId() == gViewId) {
                return view;
            }
        }

        return nullptr;
    }

    nlohmann::json ParseArgs(const std::string &payload) {
        if (payload.empty() || payload == "null") {
            return nlohmann::json::array();
        }

        try {
            auto parsed = nlohmann::json::parse(payload);

            if (parsed.is_array()) {
                return parsed;
            }

            if (parsed.is_null()) {
                return nlohmann::json::array();
            }

            return nlohmann::json::array({parsed});
        } catch (const std::exception &) {
            return nlohmann::json::array();
        }
    }

    void ApplyViewFocus() {
        if (auto *target = FindView()) {
            target->Focus(gVisible && (gMenuOpen || gConsoleOpen));
        }
    }

    void SetMenuOpen(bool open) {
        if (gMenuOpen == open) {
            return;
        }

        gMenuOpen = open;

        if (open) {
            Core::SetTalkLatched(false);
        }

        ApplyViewFocus();
        Emit(Event::kUiToggle, nlohmann::json::array({open}));
    }
}
