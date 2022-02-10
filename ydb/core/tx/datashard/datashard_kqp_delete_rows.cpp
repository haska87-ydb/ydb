#include "datashard_kqp_compute.h"

#include <ydb/core/engine/mkql_keys.h>
#include <ydb/core/engine/mkql_engine_flat_host.h>
#include <ydb/core/kqp/runtime/kqp_runtime_impl.h>

#include <ydb/library/yql/minikql/computation/mkql_computation_node_holders.h> 
#include <ydb/library/yql/minikql/computation/mkql_computation_node_impl.h> 
#include <ydb/library/yql/minikql/mkql_node.h> 
#include <ydb/library/yql/minikql/mkql_node_cast.h> 

#include <util/generic/cast.h>

namespace NKikimr {
namespace NMiniKQL {

using namespace NTable;
using namespace NUdf;

namespace {

class TKqpDeleteRowsWrapper : public TMutableComputationNode<TKqpDeleteRowsWrapper> {
    using TBase = TMutableComputationNode<TKqpDeleteRowsWrapper>;

public:
    class TRowResult : public TComputationValue<TRowResult> {
        using TBase = TComputationValue<TRowResult>;

    public:
        TRowResult(TMemoryUsageInfo* memInfo, const TKqpDeleteRowsWrapper& owner,
            NUdf::TUnboxedValue&& row)
            : TBase(memInfo)
            , Owner(owner)
            , Row(std::move(row)) {}

    private:
        void Apply(NUdf::IApplyContext& applyContext) const override {
            auto& engineCtx = *CheckedCast<TKqpDatashardApplyContext*>(&applyContext);

            TVector<TCell> keyTuple(Owner.KeyIndices.size());
            FillKeyTupleValue(Row, Owner.KeyIndices, Owner.RowTypes, keyTuple, Owner.Env);

            if (engineCtx.Host->IsPathErased(Owner.TableId)) {
                return;
            }

            if (!engineCtx.Host->IsMyKey(Owner.TableId, keyTuple)) {
                return;
            }

            ui64 nEraseRow = Owner.ShardTableStats.NEraseRow;

            engineCtx.Host->EraseRow(Owner.TableId, keyTuple);

            if (i64 delta = Owner.ShardTableStats.NEraseRow - nEraseRow; delta > 0) {
                Owner.TaskTableStats.NEraseRow += delta;
            }
        };

    private:
        const TKqpDeleteRowsWrapper& Owner;
        NUdf::TUnboxedValue Row;
    };

    class TRowsResult : public TComputationValue<TRowsResult> {
        using TBase = TComputationValue<TRowsResult>;

    public:
        TRowsResult(TMemoryUsageInfo* memInfo, const TKqpDeleteRowsWrapper& owner,
            NUdf::TUnboxedValue&& rows)
            : TBase(memInfo)
            , Owner(owner)
            , Rows(std::move(rows)) {}

        NUdf::EFetchStatus Fetch(NUdf::TUnboxedValue& result) final {
            NUdf::TUnboxedValue row;
            auto status = Rows.Fetch(row);

            if (status == NUdf::EFetchStatus::Ok) {
                result = NUdf::TUnboxedValuePod(new TRowResult(GetMemInfo(), Owner, std::move(row)));
            }

            return status;
        }

    private:
        const TKqpDeleteRowsWrapper& Owner;
        NUdf::TUnboxedValue Rows;
    };

    NUdf::TUnboxedValuePod DoCalculate(TComputationContext& ctx) const {
        return ctx.HolderFactory.Create<TRowsResult>(*this, RowsNode->GetValue(ctx));
    }

public:
    TKqpDeleteRowsWrapper(TComputationMutables& mutables, TKqpDatashardComputeContext& computeCtx,
        const TTableId& tableId, IComputationNode* rowsNode, TVector<NUdf::TDataTypeId> rowTypes, TVector<ui32> keyIndices, const TTypeEnvironment& env)
        : TBase(mutables)
        , TableId(tableId)
        , RowsNode(rowsNode)
        , RowTypes(std::move(rowTypes))
        , KeyIndices(std::move(keyIndices))
        , Env(env)
        , ShardTableStats(computeCtx.GetDatashardCounters())
        , TaskTableStats(computeCtx.GetTaskCounters(computeCtx.GetCurrentTaskId())) {}

private:
    void RegisterDependencies() const final {
        DependsOn(RowsNode);
    }

private:
    TTableId TableId;
    IComputationNode* RowsNode;
    const TVector<NUdf::TDataTypeId> RowTypes;
    const TVector<ui32> KeyIndices;
    const TTypeEnvironment& Env;
    TKqpTableStats& ShardTableStats;
    TKqpTableStats& TaskTableStats;
};

} // namespace

IComputationNode* WrapKqpDeleteRows(TCallable& callable, const TComputationNodeFactoryContext& ctx,
    TKqpDatashardComputeContext& computeCtx)
{
    MKQL_ENSURE_S(callable.GetInputsCount() == 2);

    auto tableNode = callable.GetInput(0);
    auto rowsNode = callable.GetInput(1);

    auto tableId = NKqp::ParseTableId(tableNode);
    auto localTableId = computeCtx.GetLocalTableId(tableId);
    MKQL_ENSURE_S(localTableId);
    auto tableKeyTypes = computeCtx.GetKeyColumnsInfo(tableId);

    auto rowType = AS_TYPE(TStructType, AS_TYPE(TStreamType, rowsNode.GetStaticType())->GetItemType());
    MKQL_ENSURE_S(tableKeyTypes.size() == rowType->GetMembersCount(), "Table key column count mismatch"
        << ", expected: " << tableKeyTypes.size()
        << ", actual: " << rowType->GetMembersCount());

    THashMap<TString, ui32> inputIndex;
    TVector<NUdf::TDataTypeId> rowTypes(rowType->GetMembersCount());
    for (ui32 i = 0; i < rowType->GetMembersCount(); ++i) {
        const auto& name = rowType->GetMemberName(i);
        MKQL_ENSURE_S(inputIndex.emplace(TString(name), i).second);

        auto memberType = rowType->GetMemberType(i);
        auto typeId = memberType->IsOptional()
            ? AS_TYPE(TDataType, AS_TYPE(TOptionalType, memberType)->GetItemType())->GetSchemeType()
            : AS_TYPE(TDataType, memberType)->GetSchemeType();

        rowTypes[i] = typeId;
    }

    TVector<ui32> keyIndices(tableKeyTypes.size());
    for (ui32 i = 0; i < tableKeyTypes.size(); i++) {
        auto it = inputIndex.find(tableKeyTypes[i].second);

        MKQL_ENSURE_S(rowTypes[it->second] == tableKeyTypes[i].first, "Key type mismatch"
            << ", column: " << tableKeyTypes[i].second
            << ", expected: " << tableKeyTypes[i].first
            << ", actual: " << rowTypes[it->second]);

        keyIndices[i] = it->second;
    }

    return new TKqpDeleteRowsWrapper(ctx.Mutables, computeCtx, tableId,
        LocateNode(ctx.NodeLocator, *rowsNode.GetNode()), std::move(rowTypes), std::move(keyIndices), ctx.Env);
}

} // namespace NMiniKQL
} // namespace NKikimr
