#include "datashard_kqp_compute.h"
#include "range_ops.h"

#include <ydb/core/kqp/runtime/kqp_transport.h>
#include <ydb/core/kqp/runtime/kqp_read_table.h>
#include <ydb/core/kqp/runtime/kqp_scan_data.h>
#include <ydb/core/tx/datashard/datashard_impl.h>

#include <ydb/library/yql/minikql/mkql_node.h> 

namespace NKikimr {
namespace NMiniKQL {

using namespace NTable;
using namespace NUdf;

TSmallVec<TTag> ExtractTags(const TSmallVec<TKqpComputeContextBase::TColumn>& columns) {
    TSmallVec<TTag> tags;
    for (const auto& column : columns) {
        tags.push_back(column.Tag);
    }
    return tags;
}

typedef IComputationNode* (*TCallableDatashardBuilderFunc)(TCallable& callable,
    const TComputationNodeFactoryContext& ctx, TKqpDatashardComputeContext& computeCtx);

struct TKqpDatashardComputationMap {
    TKqpDatashardComputationMap() {
        Map["KqpWideReadTable"] = &WrapKqpWideReadTable;
        Map["KqpWideReadTableRanges"] = &WrapKqpWideReadTableRanges;
        Map["KqpLookupTable"] = &WrapKqpLookupTable;
        Map["KqpUpsertRows"] = &WrapKqpUpsertRows;
        Map["KqpDeleteRows"] = &WrapKqpDeleteRows;
        Map["KqpEffects"] = &WrapKqpEffects;
    }

    THashMap<TString, TCallableDatashardBuilderFunc> Map;
};

TComputationNodeFactory GetKqpDatashardComputeFactory(TKqpDatashardComputeContext* computeCtx) {
    MKQL_ENSURE_S(computeCtx);
    MKQL_ENSURE_S(computeCtx->Database);

    auto computeFactory = GetKqpBaseComputeFactory(computeCtx);

    return [computeFactory, computeCtx]
        (TCallable& callable, const TComputationNodeFactoryContext& ctx) -> IComputationNode* {
            if (auto compute = computeFactory(callable, ctx)) {
                return compute;
            }

            const auto& datashardMap = Singleton<TKqpDatashardComputationMap>()->Map;
            auto it = datashardMap.find(callable.GetType()->GetName());
            if (it != datashardMap.end()) {
                return it->second(callable, ctx, *computeCtx);
            }

            return nullptr;
        };
};

typedef IComputationNode* (*TCallableScanBuilderFunc)(TCallable& callable,
    const TComputationNodeFactoryContext& ctx, TKqpScanComputeContext& computeCtx);

struct TKqpScanComputationMap {
    TKqpScanComputationMap() {
        Map["KqpWideReadTable"] = &WrapKqpScanWideReadTable;
        Map["KqpWideReadTableRanges"] = &WrapKqpScanWideReadTableRanges;
    }

    THashMap<TString, TCallableScanBuilderFunc> Map;
};

TComputationNodeFactory GetKqpScanComputeFactory(TKqpScanComputeContext* computeCtx) {
    MKQL_ENSURE_S(computeCtx);

    auto computeFactory = GetKqpBaseComputeFactory(computeCtx);

    return [computeFactory, computeCtx]
        (TCallable& callable, const TComputationNodeFactoryContext& ctx) -> IComputationNode* {
            if (auto compute = computeFactory(callable, ctx)) {
                return compute;
            }

            const auto& datashardMap = Singleton<TKqpScanComputationMap>()->Map;
            auto it = datashardMap.find(callable.GetType()->GetName());
            if (it != datashardMap.end()) {
                return it->second(callable, ctx, *computeCtx);
            }

            return nullptr;
        };
}

ui64 TKqpDatashardComputeContext::GetLocalTableId(const TTableId &tableId) const {
    MKQL_ENSURE_S(Shard);
    return Shard->GetLocalTableId(tableId);
}

TVector<std::pair<NScheme::TTypeId, TString>> TKqpDatashardComputeContext::GetKeyColumnsInfo(
        const TTableId &tableId) const
{
    MKQL_ENSURE_S(Shard);
    const NDataShard::TUserTable::TCPtr* tablePtr = Shard->GetUserTables().FindPtr(tableId.PathId.LocalPathId);
    MKQL_ENSURE_S(tablePtr);
    const NDataShard::TUserTable::TCPtr table = *tablePtr;
    MKQL_ENSURE_S(table);

    TVector<std::pair<NScheme::TTypeId, TString>> res;
    res.reserve(table->KeyColumnTypes.size());

    for (size_t i = 0 ; i < table->KeyColumnTypes.size(); i++) {
        auto col = table->Columns.at(table->KeyColumnIds[i]);
        MKQL_ENSURE_S(col.IsKey);
        MKQL_ENSURE_S(table->KeyColumnTypes[i] == col.Type);
        res.push_back({table->KeyColumnTypes[i], col.Name});
    }
    return res;
}

THashMap<TString, NScheme::TTypeId> TKqpDatashardComputeContext::GetKeyColumnsMap(const TTableId &tableId) const {
    THashMap<TString, NScheme::TTypeId> columnsMap;

    auto keyColumns = GetKeyColumnsInfo(tableId);
    for (const auto& [type, name] : keyColumns) {
        columnsMap[name] = type;
    }

    return columnsMap;
}

TString TKqpDatashardComputeContext::GetTablePath(const TTableId &tableId) const {
    MKQL_ENSURE_S(Shard);

    auto table = Shard->GetUserTables().FindPtr(tableId.PathId.LocalPathId);
    if (!table) {
        return TStringBuilder() << tableId;
    }

    return (*table)->Path;
}

const NDataShard::TUserTable* TKqpDatashardComputeContext::GetTable(const TTableId& tableId) const {
    MKQL_ENSURE_S(Shard);
    auto ptr = Shard->GetUserTables().FindPtr(tableId.PathId.LocalPathId);
    MKQL_ENSURE_S(ptr);
    return ptr->Get();
}

void TKqpDatashardComputeContext::ReadTable(const TTableId& tableId, const TTableRange& range) const {
    MKQL_ENSURE_S(Shard);
    Shard->SysLocksTable().SetLock(tableId, range, LockTxId);
    Shard->SetTableAccessTime(tableId, Now);
}

void TKqpDatashardComputeContext::ReadTable(const TTableId& tableId, const TArrayRef<const TCell>& key) const {
    MKQL_ENSURE_S(Shard);
    Shard->SysLocksTable().SetLock(tableId, key, LockTxId);
    Shard->SetTableAccessTime(tableId, Now);
}

void TKqpDatashardComputeContext::BreakSetLocks() const {
    MKQL_ENSURE_S(Shard);
    Shard->SysLocksTable().BreakSetLocks(LockTxId);
}

void TKqpDatashardComputeContext::SetLockTxId(ui64 lockTxId) {
    LockTxId = lockTxId;
}

ui64 TKqpDatashardComputeContext::GetShardId() const {
    return Shard->TabletID();
}

void TKqpDatashardComputeContext::SetReadVersion(TRowVersion readVersion) {
    ReadVersion = readVersion;
}

TRowVersion TKqpDatashardComputeContext::GetReadVersion() const {
    Y_VERIFY(!ReadVersion.IsMin(), "Cannot perform reads without ReadVersion set");

    return ReadVersion;
}

void TKqpDatashardComputeContext::SetTaskOutputChannel(ui64 taskId, ui64 channelId, TActorId actorId) {
    OutputChannels.emplace(std::make_pair(taskId, channelId), actorId);
}

TActorId TKqpDatashardComputeContext::GetTaskOutputChannel(ui64 taskId, ui64 channelId) const {
    auto it = OutputChannels.find(std::make_pair(taskId, channelId));
    if (it != OutputChannels.end()) {
        return it->second;
    }
    return TActorId();
}

void TKqpDatashardComputeContext::Clear() {
    Database = nullptr;
    LockTxId = 0;
}

bool TKqpDatashardComputeContext::PinPages(const TVector<IEngineFlat::TValidatedKey>& keys, ui64 pageFaultCount) {
    ui64 limitMultiplier = 1;
    if (pageFaultCount >= 2) {
        if (pageFaultCount <= 63) {
            limitMultiplier <<= pageFaultCount - 1;
        } else {
            limitMultiplier = Max<ui64>();
        }
    }

    auto adjustLimit = [limitMultiplier](ui64 limit) -> ui64 {
        if (limit >= Max<ui64>() / limitMultiplier) {
            return Max<ui64>();
        } else {
            return limit * limitMultiplier;
        }
    };

    bool ret = true;
    auto& scheme = Database->GetScheme();

    for (const auto& vKey : keys) {
        const TKeyDesc& key = *vKey.Key;

        if (TSysTables::IsSystemTable(key.TableId)) {
            continue;
        }

        if (key.RowOperation != TKeyDesc::ERowOperation::Read) {
            continue;
        }

        ui64 localTid = GetLocalTableId(key.TableId);
        Y_VERIFY(localTid, "table not exist");

        auto* tableInfo = scheme.GetTableInfo(localTid);
        TSmallVec<TRawTypeValue> from;
        TSmallVec<TRawTypeValue> to;
        ConvertTableKeys(scheme, tableInfo, key.Range.From, from, nullptr);
        if (!key.Range.Point) {
            ConvertTableKeys(scheme, tableInfo, key.Range.To, to, nullptr);
        }

        TSmallVec<NTable::TTag> columnTags;
        for (const auto& column : key.Columns) {
            if (Y_LIKELY(column.Operation == TKeyDesc::EColumnOperation::Read)) {
                columnTags.push_back(column.Column);
            }
        }
        Y_VERIFY_DEBUG(!columnTags.empty());

        bool ready = Database->Precharge(localTid,
                                         from,
                                         key.Range.Point ? from : to,
                                         columnTags,
                                         0 /* readFlags */,
                                         adjustLimit(key.RangeLimits.ItemsLimit),
                                         adjustLimit(key.RangeLimits.BytesLimit),
                                         key.Reverse ? NTable::EDirection::Reverse : NTable::EDirection::Forward,
                                         GetReadVersion());

        LOG_TRACE_S(*TlsActivationContext, NKikimrServices::TX_DATASHARD, "Run precharge on table " << tableInfo->Name
            << ", columns: [" << JoinSeq(", ", columnTags) << "]"
            << ", range: " << DebugPrintRange(key.KeyColumnTypes, key.Range, *AppData()->TypeRegistry)
            << ", itemsLimit: " << key.RangeLimits.ItemsLimit
            << ", bytesLimit: " << key.RangeLimits.BytesLimit
            << ", reverse: " << key.Reverse
            << ", result: " << ready);

        ret &= ready;
    }

    return ret;
}

} // namespace NMiniKQL
} // namespace NKikimr
