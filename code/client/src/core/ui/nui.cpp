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
        int gViewId = -1;

        // Whether the view is on screen at all.
        bool gVisible = false;

        // Whether the page says its menu panel is open. Separate from the above:
        // the interface stays up in game for the console and the netgraph, and
        // only the menu decides whether it wants the mouse.
        bool gMenuOpen = true;

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

        // LEFT HIDDEN FOR NOW. The page loads and CEF starts painting it, but
        // it stays off screen until the game has finished its own startup - a
        // connect menu over the copyright notice is the mod talking over the
        // thing it is legally obliged to show.
        //
        // A view is created hidden anyway: both Render and SubmitImGuiDraw
        // return immediately while _shouldDisplay is false, which is worth
        // knowing because it looks exactly like CEF failing to work at all.
        view->Display(false);

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

                gMenuOpen = fields.value("visible", true);

                // The page opening or closing its menu is what decides whether
                // the interface wants the pointer.
                if (auto *target = FindView()) {
                    target->Focus(gMenuOpen);
                }
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
        view->AddEventListener("vanta:menu:display", [](const std::string &payload) {
            try {
                const auto parsed  = nlohmann::json::parse(payload);
                const auto &fields = parsed.contains("payload") ? parsed["payload"] : parsed;

                // Saved, not applied. The device was created at the old size and
                // recreating it means tearing down every texture the game and
                // CEF hold - which is a device reset in the middle of a frame.
                // The player gets the new size on the next start, which is what
                // the game's own video options do too.
                Core::SaveDisplaySettings(fields.value("windowWidth", 0u), fields.value("windowHeight", 0u), fields.value("fullscreen", false));
            }
            catch (const std::exception &error) {
                Framework::Logging::GetLogger("NUI")->error("Malformed display settings: {}", error.what());
            }
        });

        Framework::Logging::GetLogger("NUI")->info("Interface up at {}x{}", width, height);
    }

    void SetInterfaceVisible(bool visible) {
        gVisible = visible;

        if (auto *view = FindView()) {
            view->Display(visible);

            // FOCUS FOLLOWS THE MENU, NOT THE VIEW. An unfocused view is never
            // told about mouse or keyboard messages even while the host is
            // forwarding them, and the manager only mirrors a cursor shape for
            // a focused one - so a focused view is what makes the interface
            // clickable. But focusing it whenever it is merely VISIBLE takes
            // the mouse away from the game, because the console and the
            // netgraph stay up in play.
            view->Focus(visible && gMenuOpen);
        }
    }

    bool IsInterfaceVisible() {
        return gVisible;
    }

    bool IsMenuOpen() {
        return gVisible && gMenuOpen;
    }

    void ReportConnected(const std::string &serverName) {
        // The page closes its own menu on this, and leaves the console and the
        // netgraph where they are. Hiding the view instead would take all three
        // away, which is what it used to do.
        gMenuOpen = false;

        if (auto *view = FindView()) {
            view->Focus(false);

            nlohmann::json payload;
            payload["name"] = serverName;

            view->EvaluateScript("window.__vantaDispatch && window.__vantaDispatch('vanta:connected', " + nlohmann::json(payload.dump()).dump() + ");");
        }
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
