#include "nui.h"

#include "core/application.h"
#include "core/client_config.h"
#include "core/globals.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <gui/manager.h>
#include <gui/view.h>
#include <logging/logger.h>

#include <nlohmann/json.hpp>

#include <filesystem>

namespace VantaMP::UI {
    namespace {
        int gViewId    = -1;
        bool gVisible  = true;

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

    } // namespace

    void BringUpInterface(int width, int height) {
        if (gViewId >= 0) {
            return;
        }

        auto *manager = Framework::CoreModules::GetWebManager();

        if (!manager || !manager->IsInitialized()) {
            Framework::Logging::GetLogger("NUI")->error("The web manager is not up; there will be no interface");
            return;
        }

        // file://, not a served URL. The page is one self-contained HTML with
        // every script, style and font inlined, precisely so it needs no origin
        // and no paths to resolve.
        const auto page = std::filesystem::path(Core::gProjectPath) / "ui" / "index.html";

        if (!std::filesystem::exists(page)) {
            Framework::Logging::GetLogger("NUI")->error("{} is missing; there will be no interface", page.string());
            return;
        }

        const auto url = "file:///" + page.generic_string();

        gViewId = manager->CreateView(url, width, height);

        if (gViewId < 0) {
            Framework::Logging::GetLogger("NUI")->error("The interface view could not be created");
            return;
        }

        auto *view = FindView();

        if (!view) {
            return;
        }

        // SHOWN, BECAUSE A VIEW IS CREATED HIDDEN. Both Render and
        // SubmitImGuiDraw return immediately while _shouldDisplay is false, so
        // without this CEF initialises, the page loads, the log says the
        // interface is up - and the screen stays black.
        view->Display(true);

        // AND FOCUSED, OR NOTHING IS CLICKABLE. A view that does not have focus
        // is not told about mouse or keyboard messages even when the host is
        // forwarding them, and the manager only mirrors a cursor shape for a
        // focused view - so an unfocused menu is visible, pointer-less and
        // completely inert.
        view->Focus(true);

        // An invoke carries an id the page is waiting on; answering it is what
        // stops a promise hanging until its timeout.
        const auto answer = [](Framework::GUI::View *target, const std::string &payload, const nlohmann::json &result) {
            try {
                const auto parsed = nlohmann::json::parse(payload);

                if (!parsed.contains("__id")) {
                    return;
                }

                target->EvaluateScript("window.__vantaResolve && window.__vantaResolve(" + std::to_string(parsed["__id"].get<int>()) + ", " + nlohmann::json(result.dump()).dump() + ");");
            }
            catch (const std::exception &) {
                // Nothing to answer, which is the same as an emit.
            }
        };

        view->AddEventListener("vanta:connect", [](const std::string &payload) {
            std::string address;
            std::string nickname = "Player";

            try {
                const auto parsed  = nlohmann::json::parse(payload);
                const auto &fields = parsed.contains("payload") ? parsed["payload"] : parsed;

                address  = fields.value("address", "");
                nickname = fields.value("nickname", nickname);
            }
            catch (const std::exception &error) {
                Framework::Logging::GetLogger("NUI")->error("Malformed connect request: {}", error.what());
                return;
            }

            if (address.empty()) {
                return;
            }

            Core::RequestConnect(address, nickname);
        });

        view->AddEventListener("vanta:menu:visible", [](const std::string &payload) {
            try {
                const auto parsed  = nlohmann::json::parse(payload);
                const auto &fields = parsed.contains("payload") ? parsed["payload"] : parsed;

                gVisible = fields.value("visible", true);
            }
            catch (const std::exception &) {
                // A malformed message is not worth changing state over.
            }
        });

        // ANSWERED, EVEN THOUGH THERE IS NOTHING TO SAY YET. The page awaits
        // these on load; leaving them unanswered is what produced "invoke
        // vanta:servers failed" before the promise even timed out.
        view->AddEventListener("vanta:servers", [view, answer](const std::string &payload) {
            answer(view, payload, nlohmann::json::array());
        });

        view->AddEventListener("vanta:menu:settings", [view, answer](const std::string &payload) {
            // The display settings the page offers are the client's own, and it
            // reads them back out of this rather than guessing.
            const auto &config = Core::GetClientConfig();

            // EVERY FIELD THE PAGE READS, not just the ones this host cares
            // about. It seeds its own inputs from this reply, so an omitted
            // `lastServer` leaves the address box holding undefined - and the
            // first click reports "cannot read properties of undefined", which
            // says nothing about a settings reply.
            nlohmann::json settings;
            settings["nickname"]     = config.nickname;
            settings["lastServer"]   = config.serverHost + ":" + std::to_string(config.serverPort);
            settings["windowWidth"]  = config.displayWidth;
            settings["windowHeight"] = config.displayHeight;
            settings["fullscreen"]   = config.displayBorderless;
            settings["display"]      = 0;
            settings["modes"]        = nlohmann::json::array();
            settings["displays"]     = nlohmann::json::array();

            answer(view, payload, settings);
        });

        view->AddEventListener("vanta:console:command", [](const std::string &payload) {
            try {
                const auto parsed  = nlohmann::json::parse(payload);
                const auto &fields = parsed.contains("payload") ? parsed["payload"] : parsed;

                Framework::Logging::GetLogger("Console")->info("{}", fields.value("command", ""));
            }
            catch (const std::exception &) {
            }
        });

        view->AddEventListener("vanta:console:backlog", [view, answer](const std::string &payload) {
            // Nothing kept yet - the client logs to a file, and the console
            // shows what arrives from here on. An empty array is still an
            // answer: the page waits on this one and times out without it.
            answer(view, payload, nlohmann::json::array());
        });

        view->AddEventListener("vanta:netgraph:enable", [](const std::string &) {});
        view->AddEventListener("vanta:console:focus", [](const std::string &) {});
        view->AddEventListener("vanta:menu:display", [](const std::string &) {});

        Framework::Logging::GetLogger("NUI")->info("Interface up at {}x{}", width, height);
    }

    void SetMenuVisible(bool visible) {
        gVisible = visible;

        if (auto *view = FindView()) {
            view->Display(visible);

            // Focus follows visibility: the world takes the input back when the
            // menu goes away.
            view->Focus(visible);

            view->EvaluateScript(visible ? "window.__vantaDispatch && window.__vantaDispatch('vanta:menu:show', '{}');" : "window.__vantaDispatch && window.__vantaDispatch('vanta:menu:hide', '{}');");
        }
    }

    bool IsMenuVisible() {
        return gVisible;
    }

    void ReportConnectionState(const std::string &state, const std::string &detail) {
        auto *view = FindView();

        if (!view) {
            return;
        }

        nlohmann::json payload;
        payload["state"]  = state;
        payload["detail"] = detail;

        // Serialised twice on purpose: once into the payload the page parses,
        // and once into a JS string literal so a quote in a server's message
        // cannot end the statement early.
        view->EvaluateScript("window.__vantaDispatch && window.__vantaDispatch('vanta:connection', " + nlohmann::json(payload.dump()).dump() + ");");
    }

    void ShutdownInterface() {
        if (gViewId < 0) {
            return;
        }

        if (auto *manager = Framework::CoreModules::GetWebManager()) {
            manager->DestroyView(gViewId);
        }

        gViewId = -1;
    }
} // namespace VantaMP::UI
