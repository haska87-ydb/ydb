#pragma once
#include "fetcher.h"

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/actorid.h>
#include <library/cpp/actors/core/events.h>
#include <library/cpp/actors/core/actor_virtual.h>
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/object_factory/object_factory.h>
#include <ydb/core/base/events.h>
#include <ydb/library/accessor/accessor.h>

namespace NKikimr::NMetadata::NProvider {

enum EEvents {
    EvRefreshSubscriberData = EventSpaceBegin(TKikimrEvents::ES_METADATA_PROVIDER),
    EvRefresh,
    EvEnrichSnapshotResult,
    EvEnrichSnapshotProblem,
    EvAskLocal,
    EvSubscribeLocal,
    EvUnsubscribeLocal,
    EvAskExternal,
    EvSubscribeExternal,
    EvUnsubscribeExternal,
    EvYQLResponse,
    EvAlterObjects,
    EvPrepareManager,
    EvManagerPrepared,
    EvTimeout,
    EvTableDescriptionFailed,
    EvTableDescriptionSuccess,
    EvEnd
};

static_assert(EEvents::EvEnd < EventSpaceEnd(TKikimrEvents::ES_METADATA_PROVIDER), "expect EvEnd < EventSpaceEnd(TKikimrEvents::ES_METADATA_PROVIDER)");

class TEvRefreshSubscriberData: public NActors::TEventLocal<TEvRefreshSubscriberData, EvRefreshSubscriberData> {
private:
    YDB_READONLY_DEF(NFetcher::ISnapshot::TPtr, Snapshot);
public:
    TEvRefreshSubscriberData(NFetcher::ISnapshot::TPtr snapshot)
        : Snapshot(snapshot) {

    }

    template <class TSnapshot>
    const TSnapshot* GetSnapshotAs() const {
        return dynamic_cast<const TSnapshot*>(Snapshot.get());
    }

    template <class TSnapshot>
    std::shared_ptr<TSnapshot> GetSnapshotPtrAs() const {
        return std::dynamic_pointer_cast<TSnapshot>(Snapshot);
    }

    template <class TSnapshot>
    std::shared_ptr<TSnapshot> GetValidatedSnapshotAs() const {
        auto result = dynamic_pointer_cast<TSnapshot>(Snapshot);
        Y_VERIFY(result);
        return result;
    }
};

}
