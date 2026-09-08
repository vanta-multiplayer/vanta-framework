#include "manager.h"

#include "shared/eos/runtime_config.h"

#include <logging/logger.h>
#include <utils/safe_win32.h>
#include <ShlObj.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#if defined(VANTAMP_HAS_EOS)
#include <eos_anticheatclient.h>
#include <eos_connect.h>
#include <eos_init.h>
#include <eos_logging.h>
#include <eos_sdk.h>
#endif

static_assert(sizeof(void *) == 4, "GTA:SA client must be built as x86");

namespace VantaMP::Eos {
    namespace {
        Manager gManager;

#if defined(VANTAMP_HAS_EOS)
        State gState           = State::Uninitialized;
        AuthSource gAuthSource = AuthSource::None;
        Shared::Eos::RuntimeConfig gConfig;
        std::string gDisplayName = "Player";
        std::string gProductUserId;
        std::string gConnectJwt;
        std::string gInstallId;
        std::string gInstallSecret;
        std::string gOpenIdAccessToken;
        std::string gProjectPath;
        std::string gServerAccountId; // informational only; EOS sub comes from UserInfo

        EOS_HPlatform        gPlatform = nullptr;
        EOS_HConnect         gConnect  = nullptr;
        EOS_HAntiCheatClient gAc       = nullptr;
        EOS_ContinuanceToken gConnectContinuance = nullptr;
        EOS_NotificationId   gIntegrityNotify    = EOS_INVALID_NOTIFICATIONID;
        std::chrono::steady_clock::time_point gNextAuthAttempt {};
        bool gAuthAttemptInFlight = false;

        const char *ResultString(EOS_EResult result) {
            const char *text = EOS_EResult_ToString(result);
            return text ? text : "EOS_Unknown";
        }

        const char *AuthSourceName(AuthSource source) {
            switch (source) {
            case AuthSource::VantaOpenId: return "VantaOpenId";
            case AuthSource::None:
            default: return "None";
            }
        }

        void EOS_CALL OnEosLog(const EOS_LogMessage *message) {
            if (!message || !message->Message) {
                return;
            }
            // Never echo bearer/access tokens if a category includes them.
            if (std::strstr(message->Message, "eyJ") != nullptr || std::strstr(message->Message, "Bearer ") != nullptr) {
                Framework::Logging::GetLogger("EOS")->info("[{}] <redacted token-bearing message>",
                                                           message->Category ? message->Category : "EOS");
                return;
            }
            Framework::Logging::GetLogger("EOS")->info("[{}] {}", message->Category ? message->Category : "EOS", message->Message);
        }

        void CacheConnectIdToken(EOS_ProductUserId userId);
        void BeginVantaOpenIdConnect();

        void EOS_CALL OnClientIntegrityViolated(const EOS_AntiCheatClient_OnClientIntegrityViolatedCallbackInfo *info) {
            if (!info) {
                return;
            }
            Framework::Logging::GetLogger("EAC")->error(
                "Integrity violation type={} message={}", static_cast<int>(info->ViolationType),
                info->ViolationMessage ? info->ViolationMessage : "");
        }

        void RegisterIntegrityNotify() {
            if (!gAc || gIntegrityNotify != EOS_INVALID_NOTIFICATIONID) {
                return;
            }
            EOS_AntiCheatClient_AddNotifyClientIntegrityViolatedOptions options {};
            options.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYCLIENTINTEGRITYVIOLATED_API_LATEST;
            gIntegrityNotify =
                EOS_AntiCheatClient_AddNotifyClientIntegrityViolated(gAc, &options, nullptr, OnClientIntegrityViolated);
        }

        std::string RandomHex(std::size_t bytes) {
            static constexpr char kHex[] = "0123456789abcdef";
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, 15);
            std::string id;
            id.reserve(bytes * 2);
            for (std::size_t i = 0; i < bytes * 2; ++i) {
                id.push_back(kHex[dist(gen)]);
            }
            return id;
        }

        std::string InstallStorePath() {
            char appData[MAX_PATH] {};
            if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) {
                return std::string(appData) + "\\VantaMP\\install.json";
            }
            return gProjectPath.empty() ? "install.json" : (gProjectPath + "\\install.json");
        }

        bool LoadOrCreateInstallCredentials() {
            const auto path = InstallStorePath();
            {
                std::ifstream in(path);
                if (in) {
                    try {
                        nlohmann::json document;
                        in >> document;
                        // Current format + one-shot migration from early develop keys.
                        gInstallId = document.value("installationId", document.value("installation_id", document.value("install_id", "")));
                        gInstallSecret =
                            document.value("secret", document.value("installationSecret", document.value("install_secret", "")));
                        const bool legacyAccountOnly = document.contains("account_id") && gInstallId.empty();
                        if (!legacyAccountOnly && !gInstallId.empty() && gInstallSecret.size() >= 32) {
                            return true;
                        }
                        Framework::Logging::GetLogger("EOS")->info(
                            "Invalidating legacy install credentials; re-registering with Vanta Auth Backend");
                    }
                    catch (...) {
                    }
                }
            }

            gInstallId     = RandomHex(16);
            gInstallSecret = RandomHex(32);
            try {
                const auto slash = path.find_last_of("\\/");
                if (slash != std::string::npos) {
                    CreateDirectoryA(path.substr(0, slash).c_str(), nullptr);
                }
                nlohmann::json document;
                document["installationId"] = gInstallId;
                document["secret"]         = gInstallSecret;
                std::ofstream out(path, std::ios::trunc);
                out << document.dump(2);
                Framework::Logging::GetLogger("EOS")->info("Created silent install credentials at {}", path);
            }
            catch (...) {
                Framework::Logging::GetLogger("EOS")->error("Failed to persist install credentials to {}", path);
                return false;
            }
            return true;
        }

        bool PostJson(const std::string &path, const nlohmann::json &body, nlohmann::json &outDocument, int &outStatus) {
            httplib::Client client(gConfig.authUrl);
            client.set_connection_timeout(5, 0);
            client.set_read_timeout(5, 0);

            const auto response = client.Post(path.c_str(), body.dump(), "application/json");
            if (!response) {
                outStatus = 0;
                return false;
            }
            outStatus = response->status;
            try {
                outDocument = nlohmann::json::parse(response->body.empty() ? "{}" : response->body);
            }
            catch (...) {
                outDocument = nlohmann::json::object();
            }
            return true;
        }

        // Prefer { ok:true, data:{...} } envelope; fall back to flat JSON for older responses.
        nlohmann::json ResponseData(const nlohmann::json &document) {
            if (document.is_object() && document.value("ok", false) && document.contains("data") && document["data"].is_object()) {
                return document["data"];
            }
            return document;
        }

        bool EnsureRegisteredWithAuthBackend() {
            nlohmann::json body;
            body["installationId"]     = gInstallId;
            body["installationSecret"] = gInstallSecret;
            body["displayName"]        = gDisplayName;

            nlohmann::json document;
            int status = 0;
            if (!PostJson("/identity/register", body, document, status)) {
                Framework::Logging::GetLogger("EOS")->error(
                    "Auth register failed (is Vanta Auth Backend running at {}?)", gConfig.authUrl);
                return false;
            }
            if (status != 200 && status != 201) {
                Framework::Logging::GetLogger("EOS")->error("Auth register HTTP {}", status);
                return false;
            }
            const auto data    = ResponseData(document);
            gServerAccountId = data.value("accountId", data.value("account_id", ""));
            Framework::Logging::GetLogger("EOS")->info(
                "Auth register OK (created={} account id length={})", data.value("created", false),
                gServerAccountId.size());
            return true;
        }

        bool FetchVantaAccessToken() {
            if (gConfig.authUrl.empty()) {
                Framework::Logging::GetLogger("EOS")->error("auth_url / auth_service_url is empty; cannot mint Vanta OpenID token");
                return false;
            }
            if (!LoadOrCreateInstallCredentials()) {
                return false;
            }

            // Register is idempotent for the same installation credential.
            if (!EnsureRegisteredWithAuthBackend()) {
                return false;
            }

            Framework::Logging::GetLogger("EOS")->info("Requesting opaque OpenID token from {}/identity/token", gConfig.authUrl);

            nlohmann::json body;
            body["installationId"]     = gInstallId;
            body["installationSecret"] = gInstallSecret;
            body["displayName"]        = gDisplayName;

            nlohmann::json document;
            int status = 0;
            if (!PostJson("/identity/token", body, document, status)) {
                Framework::Logging::GetLogger("EOS")->error(
                    "Identity token request failed (start Vanta Auth Backend at {} — not the game server)", gConfig.authUrl);
                return false;
            }
            if (status != 200) {
                Framework::Logging::GetLogger("EOS")->error("Identity token HTTP {}", status);
                return false;
            }

            const auto data      = ResponseData(document);
            gOpenIdAccessToken = data.value("accessToken", data.value("access_token", ""));
            gServerAccountId   = data.value("accountId", data.value("account_id", gServerAccountId));

            if (gOpenIdAccessToken.empty()) {
                Framework::Logging::GetLogger("EOS")->error("Identity token response missing accessToken");
                return false;
            }

            Framework::Logging::GetLogger("EOS")->info(
                "Opaque OpenID access token acquired (silent); expiresIn={} account id length={}",
                data.value("expiresIn", data.value("expires_in", 0)), gServerAccountId.size());
            return true;
        }

        void EOS_CALL OnCreateUser(const EOS_Connect_CreateUserCallbackInfo *info) {
            gAuthAttemptInFlight = false;
            if (!info) {
                gState = State::Failed;
                return;
            }

            Framework::Logging::GetLogger("EOS")->info("EOS_Connect_CreateUser callback: {}", ResultString(info->ResultCode));
            if (info->ResultCode != EOS_EResult::EOS_Success || !info->LocalUserId) {
                Framework::Logging::GetLogger("EOS")->error("Connect CreateUser failed: {}", ResultString(info->ResultCode));
                gState = State::Failed;
                return;
            }

            CacheConnectIdToken(info->LocalUserId);
        }

        void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo *info) {
            if (!info) {
                gAuthAttemptInFlight = false;
                gState               = State::Failed;
                return;
            }

            Framework::Logging::GetLogger("EOS")->info(
                "EOS_Connect_Login callback: result={} hasLocalUserId={} hasContinuanceToken={}",
                ResultString(info->ResultCode), info->LocalUserId != nullptr, info->ContinuanceToken != nullptr);

            if (info->ResultCode == EOS_EResult::EOS_Success && info->LocalUserId) {
                gAuthAttemptInFlight = false;
                CacheConnectIdToken(info->LocalUserId);
                return;
            }

            if (info->ResultCode == EOS_EResult::EOS_InvalidUser && info->ContinuanceToken) {
                Framework::Logging::GetLogger("EOS")->info(
                    "EOS_InvalidUser + ContinuanceToken → EOS_Connect_CreateUser (first-time OpenID user)");
                gConnectContinuance = info->ContinuanceToken;

                EOS_Connect_CreateUserOptions options {};
                options.ApiVersion       = EOS_CONNECT_CREATEUSER_API_LATEST;
                options.ContinuanceToken = gConnectContinuance;
                EOS_Connect_CreateUser(gConnect, &options, nullptr, OnCreateUser);
                Framework::Logging::GetLogger("EOS")->info("EOS_Connect_CreateUser submitted");
                return;
            }

            gAuthAttemptInFlight = false;
            gOpenIdAccessToken.clear();

            if (info->ResultCode == EOS_EResult::EOS_AlreadyPending) {
                Framework::Logging::GetLogger("EOS")->info("Connect login already pending");
                gAuthAttemptInFlight = true;
                return;
            }

            Framework::Logging::GetLogger("EOS")->error(
                "Connect login failed: {} — verify Epic OpenID IdP UserInfo is public HTTPS "
                "(not localhost) and Client policy allows Connect+OpenID",
                ResultString(info->ResultCode));
            gState           = State::PlatformReady;
            gNextAuthAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }

        void BeginVantaOpenIdConnect() {
            if (!gConnect || gAuthAttemptInFlight || gState == State::Connected) {
                return;
            }

            gAuthAttemptInFlight = true;
            gState               = State::Authenticating;
            if (!FetchVantaAccessToken()) {
                gAuthAttemptInFlight = false;
                gState               = State::PlatformReady;
                gNextAuthAttempt     = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                return;
            }

            Framework::Logging::GetLogger("EOS")->info(
                "Starting Connect login: type=EOS_ECT_OPENID_ACCESS_TOKEN token_len={} UserLoginInfo=NULL",
                gOpenIdAccessToken.size());

            EOS_Connect_Credentials credentials {};
            credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
            credentials.Type       = EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN;
            credentials.Token      = gOpenIdAccessToken.c_str();

            // OpenID must NOT pass UserLoginInfo — SDK requires NULL for OpenID
            // (UserLoginInfo is only for Amazon/Apple/Google/Nintendo/Oculus/DeviceID).
            EOS_Connect_LoginOptions options {};
            options.ApiVersion    = EOS_CONNECT_LOGIN_API_LATEST;
            options.Credentials   = &credentials;
            options.UserLoginInfo = nullptr;

            EOS_Connect_Login(gConnect, &options, nullptr, OnConnectLogin);
            Framework::Logging::GetLogger("EOS")->info("EOS_Connect_Login submitted (await callback)");
        }

        void CacheConnectIdToken(EOS_ProductUserId userId) {
            if (!gConnect || !userId || !EOS_ProductUserId_IsValid(userId)) {
                Framework::Logging::GetLogger("EOS")->error("Connect returned invalid ProductUserId");
                gState = State::Failed;
                return;
            }

            EOS_Connect_CopyIdTokenOptions options {};
            options.ApiVersion  = EOS_CONNECT_COPYIDTOKEN_API_LATEST;
            options.LocalUserId = userId;

            EOS_Connect_IdToken *token = nullptr;
            const auto result          = EOS_Connect_CopyIdToken(gConnect, &options, &token);
            if (result != EOS_EResult::EOS_Success || !token || !token->JsonWebToken) {
                Framework::Logging::GetLogger("EOS")->error("Connect CopyIdToken failed: {}", ResultString(result));
                gState = State::Failed;
                return;
            }

            char puid[EOS_PRODUCTUSERID_MAX_LENGTH + 1] {};
            int32_t puidBytes = static_cast<int32_t>(sizeof(puid));
            EOS_ProductUserId_ToString(userId, puid, &puidBytes);

            gProductUserId = puid;
            gConnectJwt    = token->JsonWebToken;
            gAuthSource    = AuthSource::VantaOpenId;
            EOS_Connect_IdToken_Release(token);
            gOpenIdAccessToken.clear();

            gState = State::Connected;
            Framework::Logging::GetLogger("EOS")->info("Identity authenticated (EOS_ProductUserId_IsValid=true)");
            Framework::Logging::GetLogger("EOS")->info("ProductUserId: {}", gProductUserId);
            Framework::Logging::GetLogger("EOS")->info("Identity source: VantaOpenId");
            Framework::Logging::GetLogger("EOS")->info("EAC BeginSession may proceed after game connect");
        }

        void ReleasePlatform() {
            if (gAc && gIntegrityNotify != EOS_INVALID_NOTIFICATIONID) {
                EOS_AntiCheatClient_RemoveNotifyClientIntegrityViolated(gAc, gIntegrityNotify);
                gIntegrityNotify = EOS_INVALID_NOTIFICATIONID;
            }
            if (gPlatform) {
                EOS_Platform_Release(gPlatform);
                gPlatform = nullptr;
            }
            gConnect = nullptr;
            gAc      = nullptr;
        }
#endif
    } // namespace

    Manager &GetManager() {
        return gManager;
    }

    bool Manager::Initialize(const std::string &projectPath, const std::string &displayName) {
#if !defined(VANTAMP_HAS_EOS)
        (void)projectPath;
        (void)displayName;
        Framework::Logging::GetLogger("EOS")->info("SDK not linked (missing Win32 EOSSDK)");
        return false;
#else
        if (gState != State::Uninitialized && gState != State::Failed) {
            return gPlatform != nullptr;
        }

        gState       = State::Initializing;
        gAuthSource  = AuthSource::None;
        gDisplayName = displayName.empty() ? "Player" : displayName;
        gProjectPath = projectPath;
        gProductUserId.clear();
        gConnectJwt.clear();
        gOpenIdAccessToken.clear();
        gServerAccountId.clear();

        Framework::Logging::GetLogger("EOS")->info("Initializing EOS SDK");
        gConfig = Shared::Eos::LoadRuntimeConfig(projectPath);

        if (!Shared::Eos::RuntimeConfigFilled(gConfig)) {
            Framework::Logging::GetLogger("EOS")->info("Credentials incomplete; EOS disabled");
            gState = State::Uninitialized;
            return false;
        }

        if (gConfig.connectProvider != "vanta") {
            Framework::Logging::GetLogger("EOS")->error(
                "Unsupported connect_provider '{}' (only 'vanta' — silent OpenID, no Epic Account login)", gConfig.connectProvider);
            gState = State::Failed;
            return false;
        }

        EOS_InitializeOptions initOptions {};
        initOptions.ApiVersion     = EOS_INITIALIZE_API_LATEST;
        initOptions.ProductName    = gConfig.productName.c_str();
        initOptions.ProductVersion = gConfig.productVersion.c_str();

        const auto initResult = EOS_Initialize(&initOptions);
        if (initResult != EOS_EResult::EOS_Success && initResult != EOS_EResult::EOS_AlreadyConfigured) {
            Framework::Logging::GetLogger("EOS")->error("EOS_Initialize failed: {}", ResultString(initResult));
            gState = State::Failed;
            return false;
        }

        EOS_Logging_SetCallback(OnEosLog);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Warning);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Core, EOS_ELogLevel::EOS_LOG_Verbose);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Auth, EOS_ELogLevel::EOS_LOG_VeryVerbose);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Connect, EOS_ELogLevel::EOS_LOG_VeryVerbose);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_HTTP, EOS_ELogLevel::EOS_LOG_Verbose);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_AntiCheat, EOS_ELogLevel::EOS_LOG_VeryVerbose);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Ecom, EOS_ELogLevel::EOS_LOG_Off);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Friends, EOS_ELogLevel::EOS_LOG_Off);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Presence, EOS_ELogLevel::EOS_LOG_Off);

        Framework::Logging::GetLogger("EOS")->info("SDK initialized");
        Framework::Logging::GetLogger("EOS")->info(
            "Creating platform (connect_provider=vanta auth_url={} silent OpenID)", gConfig.authUrl);

        EOS_Platform_ClientCredentials credentials {};
        credentials.ClientId     = gConfig.clientId.c_str();
        credentials.ClientSecret = gConfig.clientSecret.c_str();

        EOS_Platform_Options platformOptions {};
        platformOptions.ApiVersion        = EOS_PLATFORM_OPTIONS_API_LATEST;
        platformOptions.bIsServer         = EOS_FALSE;
        platformOptions.ProductId         = gConfig.productId.c_str();
        platformOptions.SandboxId         = gConfig.sandboxId.c_str();
        platformOptions.DeploymentId      = gConfig.deploymentId.c_str();
        platformOptions.ClientCredentials = credentials;
        platformOptions.EncryptionKey     = gConfig.encryptionKey.c_str();

        gPlatform = EOS_Platform_Create(&platformOptions);
        if (!gPlatform) {
            Framework::Logging::GetLogger("EOS")->error("EOS_Platform_Create failed (check Product/Sandbox/Deployment/Client credentials)");
            gState = State::Failed;
            return false;
        }

        gConnect = EOS_Platform_GetConnectInterface(gPlatform);
        gAc      = EOS_Platform_GetAntiCheatClientInterface(gPlatform);

        if (!gConnect) {
            Framework::Logging::GetLogger("EOS")->error("Connect interface missing");
            ReleasePlatform();
            gState = State::Failed;
            return false;
        }

        {
            const auto eacRoot = projectPath + "\\EasyAntiCheat";
            Framework::Logging::GetLogger("EAC")->info("EAC runtime root: {}", eacRoot);
            if (gAc) {
                Framework::Logging::GetLogger("EAC")->info("Anti-Cheat client interface acquired");
                RegisterIntegrityNotify();
            }
            else {
                Framework::Logging::GetLogger("EAC")->info(
                    "Anti-Cheat client interface unavailable (GetAntiCheatClientInterface returned null)");
            }
        }

        Framework::Logging::GetLogger("EOS")->info("Platform created");
        gState           = State::PlatformReady;
        gNextAuthAttempt = std::chrono::steady_clock::now();
        return true;
#endif
    }

    void Manager::Tick() {
#if defined(VANTAMP_HAS_EOS)
        if (gPlatform) {
            EOS_Platform_Tick(gPlatform);
        }
        if (gPlatform && gState == State::PlatformReady && !gAuthAttemptInFlight
            && std::chrono::steady_clock::now() >= gNextAuthAttempt) {
            BeginVantaOpenIdConnect();
        }
#endif
    }

    void Manager::Shutdown() {
#if defined(VANTAMP_HAS_EOS)
        Framework::Logging::GetLogger("EOS")->info("Shutting down");
        gState = State::ShuttingDown;
        ReleasePlatform();
        EOS_Shutdown();
        gProductUserId.clear();
        gConnectJwt.clear();
        gOpenIdAccessToken.clear();
        gAuthSource = AuthSource::None;
        gState      = State::Uninitialized;
#endif
    }

    bool Manager::IsInitialized() const {
#if defined(VANTAMP_HAS_EOS)
        return gPlatform != nullptr;
#else
        return false;
#endif
    }

    bool Manager::IsConnected() const {
#if defined(VANTAMP_HAS_EOS)
        return gState == State::Connected && !gProductUserId.empty() && !gConnectJwt.empty();
#else
        return false;
#endif
    }

    bool Manager::IsEacEligible() const {
#if defined(VANTAMP_HAS_EOS)
        return IsConnected() && gAuthSource == AuthSource::VantaOpenId;
#else
        return false;
#endif
    }

    State Manager::GetState() const {
#if defined(VANTAMP_HAS_EOS)
        return gState;
#else
        return State::Uninitialized;
#endif
    }

    AuthSource Manager::GetAuthSource() const {
#if defined(VANTAMP_HAS_EOS)
        return gAuthSource;
#else
        return AuthSource::None;
#endif
    }

    const char *Manager::GetAuthSourceName() const {
#if defined(VANTAMP_HAS_EOS)
        return AuthSourceName(gAuthSource);
#else
        return "None";
#endif
    }

    const std::string &Manager::GetProductUserId() const {
#if defined(VANTAMP_HAS_EOS)
        return gProductUserId;
#else
        static const std::string empty;
        return empty;
#endif
    }

    const std::string &Manager::GetConnectJwt() const {
#if defined(VANTAMP_HAS_EOS)
        return gConnectJwt;
#else
        static const std::string empty;
        return empty;
#endif
    }

#if defined(VANTAMP_HAS_EOS)
    EOS_HPlatform Manager::GetPlatform() const {
        return gPlatform;
    }

    EOS_HConnect Manager::GetConnect() const {
        return gConnect;
    }

    EOS_HAntiCheatClient Manager::GetAntiCheatClient() const {
        return gAc;
    }
#endif
} // namespace VantaMP::Eos
