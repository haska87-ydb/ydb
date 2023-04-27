#include "config_helpers.h"
#include "configs_dispatcher.h"
#include "console_configs_subscriber.h"
#include "console.h"
#include "http.h"
#include "util.h"

#include <ydb/core/cms/console/util/config_index.h>
#include <ydb/core/cms/console/yaml_config/yaml_config.h>
#include <ydb/core/mind/tenant_pool.h>
#include <ydb/core/mon/mon.h>

#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/interconnect.h>
#include <library/cpp/actors/core/mon.h>
#include <library/cpp/actors/interconnect/interconnect.h>
#include <library/cpp/json/json_reader.h>

#include <util/generic/bitmap.h>
#include <util/generic/ptr.h>
#include <util/string/join.h>

#if defined BLOG_D || defined BLOG_I || defined BLOG_ERROR || defined BLOG_TRACE
#error log macro definition clash
#endif

#define BLOG_D(stream) LOG_DEBUG_S(*TlsActivationContext, NKikimrServices::CONFIGS_DISPATCHER, stream)
#define BLOG_N(stream) LOG_NOTICE_S(*TlsActivationContext, NKikimrServices::CONFIGS_DISPATCHER, stream)
#define BLOG_I(stream) LOG_INFO_S(*TlsActivationContext, NKikimrServices::CONFIGS_DISPATCHER, stream)
#define BLOG_W(stream) LOG_WARN_S(*TlsActivationContext, NKikimrServices::CONFIGS_DISPATCHER, stream)
#define BLOG_ERROR(stream) LOG_ERROR_S(*TlsActivationContext, NKikimrServices::CONFIGS_DISPATCHER, stream)
#define BLOG_TRACE(stream) LOG_TRACE_S(*TlsActivationContext, NKikimrServices::CONFIGS_DISPATCHER, stream)

namespace NKikimr::NConsole {

const THashSet<ui32> DYNAMIC_KINDS({
    (ui32)NKikimrConsole::TConfigItem::ActorSystemConfigItem,
    (ui32)NKikimrConsole::TConfigItem::BootstrapConfigItem,
    (ui32)NKikimrConsole::TConfigItem::CmsConfigItem,
    (ui32)NKikimrConsole::TConfigItem::CompactionConfigItem,
    (ui32)NKikimrConsole::TConfigItem::ConfigsDispatcherConfigItem,
    (ui32)NKikimrConsole::TConfigItem::FeatureFlagsItem,
    (ui32)NKikimrConsole::TConfigItem::HiveConfigItem,
    (ui32)NKikimrConsole::TConfigItem::ImmediateControlsConfigItem,
    (ui32)NKikimrConsole::TConfigItem::LogConfigItem,
    (ui32)NKikimrConsole::TConfigItem::MonitoringConfigItem,
    (ui32)NKikimrConsole::TConfigItem::NetClassifierDistributableConfigItem,
    (ui32)NKikimrConsole::TConfigItem::NodeBrokerConfigItem,
    (ui32)NKikimrConsole::TConfigItem::SchemeShardConfigItem,
    (ui32)NKikimrConsole::TConfigItem::SharedCacheConfigItem,
    (ui32)NKikimrConsole::TConfigItem::TableProfilesConfigItem,
    (ui32)NKikimrConsole::TConfigItem::TableServiceConfigItem,
    (ui32)NKikimrConsole::TConfigItem::TenantPoolConfigItem,
    (ui32)NKikimrConsole::TConfigItem::TenantSlotBrokerConfigItem,
});

const THashSet<ui32> NON_YAML_KINDS({
    (ui32)NKikimrConsole::TConfigItem::NetClassifierDistributableConfigItem,
});

class TConfigsDispatcher : public TActorBootstrapped<TConfigsDispatcher> {
private:
    using TBase = TActorBootstrapped<TConfigsDispatcher>;

    struct TConfig {
        NKikimrConfig::TConfigVersion Version;
        NKikimrConfig::TAppConfig Config;
    };

    struct TYamlVersion {
        ui64 Version;
        TMap<ui64, ui64> VolatileVersions;
    };

    /**
     * Structure to describe configs subscription shared by multiple
     * dispatcher subscribers.
     */
    struct TSubscription : public TThrRefBase {
        using TPtr = TIntrusivePtr<TSubscription>;

        TDynBitMap Kinds;
        THashSet<TActorId> Subscribers;

        // Set to true for all yaml kinds.
        // Some 'legacy' kinds, which is usually managed by some automation e.g. NetClassifierDistributableConfigItem
        // Left this field false and consume old console configs
        bool Yaml = false;

        // Set to true if there were no config update notifications
        // Last config which was delivered to all subscribers.
        TConfig CurrentConfig;

        // If any yaml config delivered to all subscribers and acknowleged by them
        // This field is set to version from this yaml config
        std::optional<TYamlVersion> YamlVersion;

        // Config update which is currently delivered to subscribers.
        THolder<TEvConsole::TEvConfigNotificationRequest> UpdateInProcess = nullptr;
        NKikimrConfig::TConfigVersion UpdateInProcessConfigVersion;
        ui64 UpdateInProcessCookie;
        std::optional<TYamlVersion> UpdateInProcessYamlVersion;

        // Subscribers who didn't respond yet to the latest config update.
        THashSet<TActorId> SubscribersToUpdate;

        bool FirstUpdate = false;
    };

    /**
     * Structure to describe subscribers. Subscriber can have multiple
     * subscriptions but we allow only single subscription per external
     * client. The only client who can have multiple subscriptions is
     * dispatcher itself.
     */
    struct TSubscriber : public TThrRefBase {
        using TPtr = TIntrusivePtr<TSubscriber>;

        TActorId Subscriber;
        THashSet<TSubscription::TPtr> Subscriptions;
        NKikimrConfig::TConfigVersion CurrentConfigVersion;
    };

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType()
    {
        return NKikimrServices::TActivity::CONFIGS_DISPATCHER_ACTOR;
    }

    TConfigsDispatcher(const NKikimrConfig::TAppConfig &config, const TMap<TString, TString> &labels);

    void Bootstrap();

    void EnqueueEvent(TAutoPtr<IEventHandle> &ev);
    void ProcessEnqueuedEvents();

    void SendUpdateToSubscriber(TSubscription::TPtr subscription, TActorId subscriber);

    TSubscription::TPtr FindSubscription(const TActorId &subscriber);
    TSubscription::TPtr FindSubscription(const TDynBitMap &kinds);

    TSubscriber::TPtr FindSubscriber(TActorId aid);

    void UpdateYamlVersion(const TSubscription::TPtr &kinds) const;

    NKikimrConfig::TAppConfig ParseYamlProtoConfig();
        
    void Handle(NMon::TEvHttpInfo::TPtr &ev);
    void Handle(TEvInterconnect::TEvNodesInfo::TPtr &ev);
    void Handle(TEvConsole::TEvConfigSubscriptionNotification::TPtr &ev);
    void Handle(TEvConsole::TEvConfigSubscriptionError::TPtr &ev);
    void Handle(TEvConsole::TEvConfigNotificationResponse::TPtr &ev);
    void Handle(TEvConfigsDispatcher::TEvGetConfigRequest::TPtr &ev);
    void Handle(TEvConfigsDispatcher::TEvSetConfigSubscriptionRequest::TPtr &ev);
    void Handle(TEvConfigsDispatcher::TEvRemoveConfigSubscriptionRequest::TPtr &ev);
    void Handle(TEvConsole::TEvConfigNotificationRequest::TPtr &ev);

    STATEFN(StateInit)
    {
        TRACE_EVENT(NKikimrServices::CONFIGS_DISPATCHER);
        switch (ev->GetTypeRewrite()) {
            // Monitoring page
            hFuncTraced(NMon::TEvHttpInfo, Handle);
            hFuncTraced(TEvInterconnect::TEvNodesInfo, Handle);
            // Updates from console
            hFuncTraced(TEvConsole::TEvConfigSubscriptionNotification, Handle);
            hFuncTraced(TEvConsole::TEvConfigSubscriptionError, Handle);
            // Events from clients
            hFuncTraced(TEvConfigsDispatcher::TEvSetConfigSubscriptionRequest, Handle);
            hFuncTraced(TEvConfigsDispatcher::TEvRemoveConfigSubscriptionRequest, Handle);
        default:
            EnqueueEvent(ev);
            break;
        }
    }

    STATEFN(StateWork)
    {
        TRACE_EVENT(NKikimrServices::CONFIGS_DISPATCHER);
        switch (ev->GetTypeRewrite()) {
            // Monitoring page
            hFuncTraced(NMon::TEvHttpInfo, Handle);
            hFuncTraced(TEvInterconnect::TEvNodesInfo, Handle);
            // Updates from console
            hFuncTraced(TEvConsole::TEvConfigSubscriptionNotification, Handle);
            hFuncTraced(TEvConsole::TEvConfigSubscriptionError, Handle);
            // Events from clients
            hFuncTraced(TEvConfigsDispatcher::TEvGetConfigRequest, Handle);
            hFuncTraced(TEvConfigsDispatcher::TEvSetConfigSubscriptionRequest, Handle);
            hFuncTraced(TEvConfigsDispatcher::TEvRemoveConfigSubscriptionRequest, Handle);
            hFuncTraced(TEvConsole::TEvConfigNotificationResponse, Handle);
            IgnoreFunc(TEvConfigsDispatcher::TEvSetConfigSubscriptionResponse);

            // Ignore these console requests until we get rid of persistent subscriptions-related code
            IgnoreFunc(TEvConsole::TEvAddConfigSubscriptionResponse);
            IgnoreFunc(TEvConsole::TEvGetNodeConfigResponse);
            // Pretend we got this
            hFuncTraced(TEvConsole::TEvConfigNotificationRequest, Handle);
        default:
            Y_FAIL("unexpected event type: %" PRIx32 " event: %s",
                   ev->GetTypeRewrite(), ev->ToString().data());
            break;
        }
    }


private:
    TMap<TString, TString> Labels;
    NKikimrConfig::TAppConfig InitialConfig;
    NKikimrConfig::TAppConfig CurrentConfig;
    ui64 NextRequestCookie;
    TVector<TActorId> HttpRequests;
    TActorId CommonSubscriptionClient;
    TDeque<TAutoPtr<IEventHandle>> EventsQueue;

    THashMap<TActorId, TSubscription::TPtr> SubscriptionsBySubscriber;
    THashMap<TDynBitMap, TSubscription::TPtr> SubscriptionsByKinds;
    THashMap<TActorId, TSubscriber::TPtr> Subscribers;

    TString YamlConfig;
    TMap<ui64, TString> VolatileYamlConfigs;
    TMap<ui64, size_t> VolatileYamlConfigHashes;
    TString ResolvedYamlConfig;
    TString ResolvedJsonConfig;
    NKikimrConfig::TAppConfig YamlProtoConfig;
    bool YamlConfigEnabled = false;
};

TConfigsDispatcher::TConfigsDispatcher(const NKikimrConfig::TAppConfig &config, const TMap<TString, TString> &labels)
    : Labels(labels)
    , InitialConfig(config)
    , CurrentConfig(config)
    , NextRequestCookie(Now().GetValue())
{
}

void TConfigsDispatcher::Bootstrap()
{
    BLOG_D("TConfigsDispatcher Bootstrap");

    NActors::TMon *mon = AppData()->Mon;
    if (mon) {
        NMonitoring::TIndexMonPage *actorsMonPage = mon->RegisterIndexPage("actors", "Actors");
        mon->RegisterActorPage(actorsMonPage, "configs_dispatcher", "Configs Dispatcher", false, TlsActivationContext->ExecutorThread.ActorSystem, SelfId());
    }

    auto commonClient = CreateConfigsSubscriber(
        SelfId(),
        TVector<ui32>(DYNAMIC_KINDS.begin(), DYNAMIC_KINDS.end()),
        CurrentConfig,
        0,
        true);
    CommonSubscriptionClient = RegisterWithSameMailbox(commonClient);

    Become(&TThis::StateInit);
}

void TConfigsDispatcher::EnqueueEvent(TAutoPtr<IEventHandle> &ev)
{
    BLOG_D("Enqueue event type: " << ev->GetTypeRewrite());
    EventsQueue.push_back(ev);
}

void TConfigsDispatcher::ProcessEnqueuedEvents()
{
    while (!EventsQueue.empty()) {
        TAutoPtr<IEventHandle> &ev = EventsQueue.front();
        BLOG_D("Dequeue event type: " << ev->GetTypeRewrite());
        TlsActivationContext->Send(ev.Release());
        EventsQueue.pop_front();
    }
}

void TConfigsDispatcher::SendUpdateToSubscriber(TSubscription::TPtr subscription, TActorId subscriber)
{
    Y_VERIFY(subscription->UpdateInProcess);

    subscription->SubscribersToUpdate.insert(subscriber);

    auto notification = MakeHolder<TEvConsole::TEvConfigNotificationRequest>();
    notification->Record.CopyFrom(subscription->UpdateInProcess->Record);

    BLOG_TRACE("Send TEvConsole::TEvConfigNotificationRequest to " << subscriber
                << ": " << notification->Record.ShortDebugString());

    Send(subscriber, notification.Release(), 0, subscription->UpdateInProcessCookie);
}

TConfigsDispatcher::TSubscription::TPtr TConfigsDispatcher::FindSubscription(const TDynBitMap &kinds)
{
    if (auto it = SubscriptionsByKinds.find(kinds); it != SubscriptionsByKinds.end())
        return it->second;

    return nullptr;
}

TConfigsDispatcher::TSubscription::TPtr TConfigsDispatcher::FindSubscription(const TActorId &id)
{
    if (auto it = SubscriptionsBySubscriber.find(id); it != SubscriptionsBySubscriber.end())
        return it->second;

    return nullptr;
}

TConfigsDispatcher::TSubscriber::TPtr TConfigsDispatcher::FindSubscriber(TActorId aid)
{
    if (auto it = Subscribers.find(aid); it != Subscribers.end())
        return it->second;

    return nullptr;
}

NKikimrConfig::TAppConfig TConfigsDispatcher::ParseYamlProtoConfig()
{
    NKikimrConfig::TAppConfig newYamlProtoConfig = {};

    NYamlConfig::ResolveAndParseYamlConfig(
        YamlConfig,
        VolatileYamlConfigs,
        Labels,
        newYamlProtoConfig,
        &ResolvedYamlConfig,
        &ResolvedJsonConfig);

    return newYamlProtoConfig;
}

void TConfigsDispatcher::Handle(NMon::TEvHttpInfo::TPtr &ev)
{
    if (HttpRequests.empty())
        Send(GetNameserviceActorId(), new TEvInterconnect::TEvListNodes);

    HttpRequests.push_back(ev->Sender);
}

void TConfigsDispatcher::Handle(TEvConsole::TEvConfigNotificationRequest::TPtr &ev)
{
    const auto &rec = ev->Get()->Record;
    auto resp = MakeHolder<TEvConsole::TEvConfigNotificationResponse>(rec);

    BLOG_TRACE("Send TEvConfigNotificationResponse: " << resp->Record.ShortDebugString());

    Send(ev->Sender, resp.Release(), 0, ev->Cookie);
}

void TConfigsDispatcher::Handle(TEvInterconnect::TEvNodesInfo::TPtr &ev)
{
    Y_UNUSED(ev);
    TStringStream str;
    str << NMonitoring::HTTPOKHTML;
    HTML(str) {
        HEAD() {
            str << "<link rel='stylesheet' href='../cms/ext/bootstrap.min.css'>" << Endl
                << "<script language='javascript' type='text/javascript' src='../cms/ext/jquery.min.js'></script>" << Endl
                << "<script language='javascript' type='text/javascript' src='../cms/ext/bootstrap.bundle.min.js'></script>" << Endl
                << "<script language='javascript' type='text/javascript'>" << Endl
                << "var nodeNames = [";

            for (auto &node: ev->Get()->Nodes) {
                str << "{'nodeName':'" << node.Host << "'}, ";
            }

            str << "];" << Endl
                << "</script>" << Endl
                << "<script src='../cms/ext/fuse.min.js'></script>" << Endl
                << "<script src='../cms/common.js'></script>" << Endl
                << "<script src='../cms/ext/fuzzycomplete.min.js'></script>" << Endl
                << "<link rel='stylesheet' href='../cms/ext/fuzzycomplete.min.css'>" << Endl
                << "<link rel='stylesheet' href='../cms/cms.css'>" << Endl
                << "<script data-main='../cms/configs_dispatcher_main' src='../cms/ext/require.min.js'></script>" << Endl;

        }
        NHttp::OutputStyles(str);

        DIV() {
            OL_CLASS("breadcrumb") {
                LI_CLASS("breadcrumb-item") {
                    str << "<a href='..' id='host-ref'>YDB Developer UI</a>" << Endl;
                }
                LI_CLASS("breadcrumb-item") {
                    str << "<a href='.'>Actors</a>" << Endl;
                }
                LI_CLASS("breadcrumb-item active") {
                    str << "Configs Dispatcher" << Endl;
                }
            }
        }

        DIV_CLASS("container") {
            DIV_CLASS("navbar navbar-expand-lg navbar-light bg-light") {
                DIV_CLASS("navbar-collapse") {
                    UL_CLASS("navbar-nav mr-auto") {
                        LI_CLASS("nav-item") {
                            str << "<a class='nav-link' href=\"../cms?#page=yaml-config\">Console</a>" << Endl;
                        }
                    }
                    FORM_CLASS("form-inline my-2 my-lg-0") {
                        str << "<input type='text' id='nodePicker' class='form-control mr-sm-2' name='nodes' placeholder='Nodes...'>" << Endl;
                        str << "<a type='button' class='btn btn-primary my-2 my-sm-0' id='nodesGo'>Go</a>" << Endl;
                    }
                }
            }
            DIV_CLASS("tab-left") {
                COLLAPSED_REF_CONTENT("node-labels", "Node labels") {
                    PRE() {
                        for (auto &[key, value] : Labels) {
                            str << key << " = " << value << Endl;
                        }
                    }
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("state", "State") {
                    PRE() {
                        str << "SelfId: " << SelfId() << Endl;
                        auto s = CurrentStateFunc();
                        str << "State: " << ( s == &TThis::StateWork      ? "StateWork"
                                            : s == &TThis::StateInit      ? "StateInit"
                                                                          : "Unknown" ) << Endl;
                        str << "YamlConfigEnabled: " << YamlConfigEnabled << Endl;
                        str << "Subscriptions: " << Endl;
                        for (auto &[kinds, subscription] : SubscriptionsByKinds) {
                            str << "- Kinds: " << KindsToString(kinds) << Endl
                                << "  Subscription: " << Endl
                                << "    Yaml: " << subscription->Yaml << Endl
                                << "    Subscribers: " << Endl;
                            for (auto &id : subscription->Subscribers) {
                                str << "    - Actor: " << id << Endl;
                            }
                            if (subscription->YamlVersion) {
                                str << "    YamlVersion: " << subscription->YamlVersion->Version << ".[";
                                bool first = true;
                                for (auto &[id, hash] : subscription->YamlVersion->VolatileVersions) {
                                    str << (first ? "" : ",") << id << "." << hash;
                                    first = false;
                                }
                                str << "]" << Endl;
                            } else {
                                str << "    CurrentConfigId: " << subscription->CurrentConfig.Version.ShortDebugString() << Endl;
                            }
                            str << "    CurrentConfig: " << subscription->CurrentConfig.Config.ShortDebugString() << Endl;
                            if (subscription->UpdateInProcess) {
                                str << "    UpdateInProcess: " << subscription->UpdateInProcess->Record.ShortDebugString() << Endl
                                    << "    SubscribersToUpdate:";
                                for (auto &id : subscription->SubscribersToUpdate) {
                                    str << " " << id;
                                }
                                str << Endl;
                                str << "    UpdateInProcessConfigVersion: " << subscription->UpdateInProcessConfigVersion.ShortDebugString() << Endl
                                    << "    UpdateInProcessCookie: " << subscription->UpdateInProcessCookie << Endl;
                                if (subscription->UpdateInProcessYamlVersion) {
                                    str << "    UpdateInProcessYamlVersion: " << subscription->UpdateInProcessYamlVersion->Version << Endl;
                                }
                            }
                        }
                        str << "Subscribers:" << Endl;
                        for (auto &[subscriber, _] : SubscriptionsBySubscriber) {
                            str << "- " << subscriber << Endl;
                        }
                    }
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("yaml-config", "YAML config") {
                    DIV() {
                        TAG(TH5) {
                            str << "Persistent Config" << Endl;
                        }
                        TAG_CLASS_STYLE(TDiv, "configs-dispatcher", "padding: 0 12px;") {
                            TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap fold-yaml-config yaml-btn-3"}, {"id", "fold-yaml-config"}, {"title", "fold"}}) {
                                DIV_CLASS("yaml-sticky-btn") { }
                            }
                            TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap unfold-yaml-config yaml-btn-2"}, {"id", "unfold-yaml-config"}, {"title", "unfold"}}) {
                                DIV_CLASS("yaml-sticky-btn") { }
                            }
                            TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap copy-yaml-config yaml-btn-1"}, {"id", "copy-yaml-config"}, {"title", "copy"}}) {
                                DIV_CLASS("yaml-sticky-btn") { }
                            }
                            TAG_ATTRS(TDiv, {{"id", "yaml-config-item"}, {"name", "yaml-config-itemm"}}) {
                                str << YamlConfig;
                            }
                        }
                        str << "<hr/>" << Endl;
                        for (auto &[id, config] : VolatileYamlConfigs) {
                            DIV() {
                                TAG(TH5) {
                                    str << "Volatile Config Id: " << id << Endl;
                                }
                                TAG_CLASS_STYLE(TDiv, "configs-dispatcher", "padding: 0 12px;") {
                                    TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap fold-yaml-config yaml-btn-3"}, {"title", "fold"}}) {
                                        DIV_CLASS("yaml-sticky-btn") { }
                                    }
                                    TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap unfold-yaml-config yaml-btn-2"}, {"title", "unfold"}}) {
                                        DIV_CLASS("yaml-sticky-btn") { }
                                    }
                                    TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap copy-yaml-config yaml-btn-1"}, {"title", "copy"}}) {
                                        DIV_CLASS("yaml-sticky-btn") { }
                                    }
                                    DIV_CLASS("yaml-config-item") {
                                        str << config;
                                    }
                                }
                            }
                        }
                    }
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("resolved-yaml-config", "Resolved YAML config") {
                    TAG_CLASS_STYLE(TDiv, "configs-dispatcher", "padding: 0 12px;") {
                        TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap fold-yaml-config yaml-btn-3"}, {"id", "fold-resolved-yaml-config"}, {"title", "fold"}}) {
                            DIV_CLASS("yaml-sticky-btn") { }
                        }
                        TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap unfold-yaml-config yaml-btn-2"}, {"id", "unfold-resolved-yaml-config"}, {"title", "unfold"}}) {
                            DIV_CLASS("yaml-sticky-btn") { }
                        }
                        TAG_ATTRS(TDiv, {{"class", "yaml-sticky-btn-wrap copy-yaml-config yaml-btn-1"}, {"id", "copy-resolved-yaml-config"}, {"title", "copy"}}) {
                            DIV_CLASS("yaml-sticky-btn") { }
                        }
                        TAG_ATTRS(TDiv, {{"id", "resolved-yaml-config-item"}, {"name", "resolved-yaml-config-itemm"}}) {
                            str << ResolvedYamlConfig;
                        }
                    }
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("resolved-json-config", "Resolved JSON config") {
                    PRE() {
                        str << ResolvedJsonConfig << Endl;
                    }
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("yaml-proto-config", "YAML proto config") {
                    NHttp::OutputConfigHTML(str, YamlProtoConfig);
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("current-config", "Current config") {
                    NHttp::OutputConfigHTML(str, CurrentConfig);
                }
                str << "<br />" << Endl;
                COLLAPSED_REF_CONTENT("initial-config", "Initial config") {
                    NHttp::OutputConfigHTML(str, InitialConfig);
                }
            }
        }
    }

    for (auto &actor : HttpRequests) {
        Send(actor, new NMon::TEvHttpInfoRes(str.Str(), 0, NMon::IEvHttpInfoRes::EContentType::Custom));
    }

    HttpRequests.clear();
}

void TConfigsDispatcher::Handle(TEvConsole::TEvConfigSubscriptionNotification::TPtr &ev)
{
    auto &rec = ev->Get()->Record;

    CurrentConfig = rec.GetConfig();

    const auto& newYamlConfig = rec.GetYamlConfig();

    bool isYamlChanged = newYamlConfig != YamlConfig;

    if (rec.VolatileConfigsSize() != VolatileYamlConfigs.size()) {
        isYamlChanged = true;
    }

    for (auto &volatileConfig : rec.GetVolatileConfigs()) {
        if (auto it = VolatileYamlConfigHashes.find(volatileConfig.GetId());
            it == VolatileYamlConfigHashes.end() || it->second != THash<TString>()(volatileConfig.GetConfig())) {
            isYamlChanged = true;
        }
    }

    if (isYamlChanged) {
        YamlConfig = newYamlConfig;
        VolatileYamlConfigs.clear();
        VolatileYamlConfigHashes.clear();
        for (auto &volatileConfig : rec.GetVolatileConfigs()) {
            VolatileYamlConfigs[volatileConfig.GetId()] = volatileConfig.GetConfig();
            VolatileYamlConfigHashes[volatileConfig.GetId()] = THash<TString>()(volatileConfig.GetConfig());
        }
    }

    NKikimrConfig::TAppConfig newYamlProtoConfig = {};

    bool yamlConfigTurnedOff = false;

    if (!YamlConfig.empty() && isYamlChanged) {
        newYamlProtoConfig = ParseYamlProtoConfig();
        bool wasYamlConfigEnabled = YamlConfigEnabled;
        YamlConfigEnabled = newYamlProtoConfig.HasYamlConfigEnabled() && newYamlProtoConfig.GetYamlConfigEnabled();
        yamlConfigTurnedOff = wasYamlConfigEnabled && !YamlConfigEnabled;
    } else if (YamlConfig.empty()) {
        bool wasYamlConfigEnabled = YamlConfigEnabled;
        YamlConfigEnabled = false;
        yamlConfigTurnedOff = wasYamlConfigEnabled && !YamlConfigEnabled;
    } else {
        newYamlProtoConfig = YamlProtoConfig;
    }

    std::swap(YamlProtoConfig, newYamlProtoConfig);

    THashSet<ui32> affectedKinds;
    for (const auto& kind : ev->Get()->Record.GetAffectedKinds()) {
        affectedKinds.insert(kind);
    }

    for (auto &[kinds, subscription] : SubscriptionsByKinds) {
        if (subscription->UpdateInProcess) {
            subscription->UpdateInProcess = nullptr;
            subscription->SubscribersToUpdate.clear();
        }

        NKikimrConfig::TAppConfig trunc;

        bool hasAffectedKinds = false;

        if (subscription->Yaml && YamlConfigEnabled) {
            ReplaceConfigItems(YamlProtoConfig, trunc, subscription->Kinds);
        } else {
            Y_FOR_EACH_BIT(kind, kinds) {
                if (affectedKinds.contains(kind)) {
                    hasAffectedKinds = true;
                }
            }

            // we try resend all configs if yaml config was turned off
            if (!hasAffectedKinds && !yamlConfigTurnedOff) {
                continue;
            }

            ReplaceConfigItems(ev->Get()->Record.GetConfig(), trunc, kinds);
        }

        subscription->FirstUpdate = true;

        if (hasAffectedKinds || !CompareConfigs(subscription->CurrentConfig.Config, trunc))
        {
            subscription->UpdateInProcess = MakeHolder<TEvConsole::TEvConfigNotificationRequest>();
            subscription->UpdateInProcess->Record.MutableConfig()->CopyFrom(trunc);
            subscription->UpdateInProcess->Record.SetLocal(true);
            Y_FOR_EACH_BIT(kind, kinds) {
                subscription->UpdateInProcess->Record.AddItemKinds(kind);
            }
            subscription->UpdateInProcessCookie = ++NextRequestCookie;
            subscription->UpdateInProcessConfigVersion = FilterVersion(ev->Get()->Record.GetConfig().GetVersion(), kinds);

            if (YamlConfigEnabled) {
                UpdateYamlVersion(subscription);
            }

            for (auto &subscriber : subscription->Subscribers) {
                auto k = kinds;
                BLOG_TRACE("Sending for kinds: " << KindsToString(k));
                SendUpdateToSubscriber(subscription, subscriber);
            }
        } else if (YamlConfigEnabled && subscription->Yaml) {
            UpdateYamlVersion(subscription);
        } else if (!YamlConfigEnabled) {
            subscription->YamlVersion = std::nullopt;
        }
    }
    
    if (CurrentStateFunc() == &TThis::StateInit) {
        Become(&TThis::StateWork);
        ProcessEnqueuedEvents();
    }
}

void TConfigsDispatcher::UpdateYamlVersion(const TSubscription::TPtr &subscription) const
{
    TYamlVersion yamlVersion;
    yamlVersion.Version = NYamlConfig::GetVersion(YamlConfig);
    for (auto &[id, hash] : VolatileYamlConfigHashes) {
        yamlVersion.VolatileVersions[id] = hash;
    }
    subscription->UpdateInProcessYamlVersion = yamlVersion;
}

void TConfigsDispatcher::Handle(TEvConsole::TEvConfigSubscriptionError::TPtr &ev)
{
    // The only reason we can get this response is ambiguous domain
    // So it is okay to fail here
    Y_FAIL("Can't start Configs Dispatcher: %s",
            ev->Get()->Record.GetReason().c_str());
}

void TConfigsDispatcher::Handle(TEvConfigsDispatcher::TEvGetConfigRequest::TPtr &ev)
{
    auto resp = MakeHolder<TEvConfigsDispatcher::TEvGetConfigResponse>();

    for (auto kind : ev->Get()->ConfigItemKinds) {
        if (!DYNAMIC_KINDS.contains(kind)) {
            TStringStream sstr;
            sstr << static_cast<NKikimrConsole::TConfigItem::EKind>(kind);
            Y_FAIL("unexpected kind in GetConfigRequest: %s", sstr.Str().data());
        }
    }

    auto trunc = std::make_shared<NKikimrConfig::TAppConfig>();
    ReplaceConfigItems(CurrentConfig, *trunc, KindsToBitMap(ev->Get()->ConfigItemKinds));
    resp->Config = trunc;

    BLOG_TRACE("Send TEvConfigsDispatcher::TEvGetConfigResponse"
        " to " << ev->Sender << ": " << resp->Config->ShortDebugString());

    Send(ev->Sender, std::move(resp), 0, ev->Cookie);
}

void TConfigsDispatcher::Handle(TEvConfigsDispatcher::TEvSetConfigSubscriptionRequest::TPtr &ev)
{
    bool yamlKinds = false;
    bool nonYamlKinds = false;
    for (auto kind : ev->Get()->ConfigItemKinds) {
        if (!DYNAMIC_KINDS.contains(kind)) {
            TStringStream sstr;
            sstr << static_cast<NKikimrConsole::TConfigItem::EKind>(kind);
            Y_FAIL("unexpected kind in SetConfigSubscriptionRequest: %s", sstr.Str().data());
        }

        if (NON_YAML_KINDS.contains(kind)) {
            nonYamlKinds = true;
        } else {
            yamlKinds = true;
        }
    }

    if (yamlKinds && nonYamlKinds) {
        Y_FAIL("both yaml and non yaml kinds in SetConfigSubscriptionRequest");
    }

    auto kinds = KindsToBitMap(ev->Get()->ConfigItemKinds);
    auto subscriberActor = ev->Get()->Subscriber ? ev->Get()->Subscriber : ev->Sender;

    auto subscription = FindSubscription(kinds);
    if (!subscription) {
        subscription = new TSubscription;
        subscription->Kinds = kinds;
        subscription->Yaml = yamlKinds;

        SubscriptionsByKinds.emplace(kinds, subscription);
    }
    subscription->Subscribers.insert(subscriberActor);
    SubscriptionsBySubscriber.emplace(subscriberActor, subscription);

    auto subscriber = FindSubscriber(subscriberActor);
    if (!subscriber) {
        subscriber = new TSubscriber;
        subscriber->Subscriber = subscriberActor;
        Subscribers.emplace(subscriberActor, subscriber);
    }
    subscriber->Subscriptions.insert(subscription);

    // We don't care about versions and kinds here
    Send(ev->Sender, new TEvConfigsDispatcher::TEvSetConfigSubscriptionResponse);

    if (subscription->FirstUpdate) {
        // first time we send even empty config
        if (!subscription->UpdateInProcess) {
            subscription->UpdateInProcess = MakeHolder<TEvConsole::TEvConfigNotificationRequest>();
            NKikimrConfig::TAppConfig trunc;
            if (YamlConfigEnabled) {
                ReplaceConfigItems(YamlProtoConfig, trunc, kinds);
            } else {
                ReplaceConfigItems(CurrentConfig, trunc, kinds);
            }
            subscription->UpdateInProcess->Record.MutableConfig()->CopyFrom(trunc);
            Y_FOR_EACH_BIT(kind, kinds) {
                subscription->UpdateInProcess->Record.AddItemKinds(kind);
            }
            subscription->UpdateInProcessCookie = ++NextRequestCookie;
            subscription->UpdateInProcessConfigVersion = FilterVersion(CurrentConfig.GetVersion(), kinds);
        }
        BLOG_TRACE("Sending for kinds: " << KindsToString(kinds));
        SendUpdateToSubscriber(subscription, subscriber->Subscriber);
    }
}

void TConfigsDispatcher::Handle(TEvConfigsDispatcher::TEvRemoveConfigSubscriptionRequest::TPtr &ev)
{
    auto subscriberActor = ev->Get()->Subscriber ? ev->Get()->Subscriber : ev->Sender;
    auto subscriber = FindSubscriber(subscriberActor);
    if (!subscriber) {
        return;
    }

    for (auto &subscription : subscriber->Subscriptions) {
        subscription->SubscribersToUpdate.erase(subscriberActor);
        if (subscription->SubscribersToUpdate.empty()) {
            if (subscription->UpdateInProcess) {
                subscription->CurrentConfig.Version = subscription->UpdateInProcessConfigVersion;
                subscription->CurrentConfig.Config = subscription->UpdateInProcess->Record.GetConfig();
            }
            subscription->YamlVersion = subscription->UpdateInProcessYamlVersion;
            subscription->UpdateInProcessYamlVersion = std::nullopt;
            subscription->UpdateInProcess = nullptr;
        }

        subscription->Subscribers.erase(subscriberActor);
        if (subscription->Subscribers.empty()) {
            SubscriptionsByKinds.erase(subscription->Kinds);
        }
    }

    Subscribers.erase(subscriberActor);
    SubscriptionsBySubscriber.erase(subscriberActor);

    Send(ev->Sender, new TEvConfigsDispatcher::TEvRemoveConfigSubscriptionResponse);
}


void TConfigsDispatcher::Handle(TEvConsole::TEvConfigNotificationResponse::TPtr &ev)
{
    auto rec = ev->Get()->Record;
    auto subscription = FindSubscription(ev->Sender);

    // Probably subscription was cleared up due to tenant's change.
    if (!subscription) {
        BLOG_ERROR("Got notification response for unknown subscription " << ev->Sender);
        return;
    }

    if (!subscription->UpdateInProcess) {
        BLOG_D("Notification was ignored for subscription " << ev->Sender);
        return;
    }

    if (ev->Cookie != subscription->UpdateInProcessCookie) {
        BLOG_ERROR("Notification cookie mismatch for subscription " << ev->Sender << " " << ev->Cookie << " != " << subscription->UpdateInProcessCookie);
        // TODO fix clients
        return;
    }

    if (!subscription->SubscribersToUpdate.contains(ev->Sender)) {
        BLOG_ERROR("Notification from unexpected subscriber for subscription " << ev->Sender);
        return;
    }

    Subscribers.at(ev->Sender)->CurrentConfigVersion = subscription->UpdateInProcessConfigVersion;

    // If all subscribers responded then send response to CMS.
    subscription->SubscribersToUpdate.erase(ev->Sender);

    if (subscription->SubscribersToUpdate.empty()) {
        subscription->CurrentConfig.Config = subscription->UpdateInProcess->Record.GetConfig();
        subscription->CurrentConfig.Version = subscription->UpdateInProcessConfigVersion;
        subscription->YamlVersion = subscription->UpdateInProcessYamlVersion;
        subscription->UpdateInProcessYamlVersion = std::nullopt;
        subscription->UpdateInProcess = nullptr;
    }
}
    
IActor *CreateConfigsDispatcher(
    const NKikimrConfig::TAppConfig &config,
    const TMap<TString, TString> &labels)
{
    return new TConfigsDispatcher(config, labels);
}

} // namespace NKikimr::NConsole
