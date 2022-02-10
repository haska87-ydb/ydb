#include "kqp_opt_log_rules.h"

#include <ydb/core/kqp/opt/kqp_opt_impl.h>
#include <ydb/core/kqp/common/kqp_yql.h>
#include <ydb/core/kqp/provider/yql_kikimr_provider_impl.h>
#include <ydb/core/kqp/provider/yql_kikimr_opt_utils.h>

#include <ydb/library/yql/core/yql_opt_utils.h> 

namespace NKikimr::NKqp::NOpt {

using namespace NYql;
using namespace NYql::NCommon;
using namespace NYql::NDq;
using namespace NYql::NNodes;

namespace {

[[maybe_unused]]
bool IsKqlPureExpr(const TExprBase& expr) {
    auto node = FindNode(expr.Ptr(), [](const TExprNode::TPtr& node) {
        return node->IsCallable()
            && (node->Content().StartsWith("Kql")
                || node->Content().StartsWith("Kqp")
                || node->Content().StartsWith("Dq"));
    });
    return node.Get() == nullptr;
}

TDqJoin FlipLeftSemiJoin(const TDqJoin& join, TExprContext& ctx) {
    Y_VERIFY_DEBUG(join.JoinType().Value() == "LeftSemi");

    auto joinKeysBuilder = Build<TDqJoinKeyTupleList>(ctx, join.Pos());
    for (const auto& keys : join.JoinKeys()) {
        joinKeysBuilder.Add<TDqJoinKeyTuple>()
            .LeftLabel(keys.RightLabel())
            .LeftColumn(keys.RightColumn())
            .RightLabel(keys.LeftLabel())
            .RightColumn(keys.LeftColumn())
            .Build();
    }

    return Build<TDqJoin>(ctx, join.Pos())
        .LeftInput(join.RightInput())
        .LeftLabel(join.RightLabel())
        .RightInput(join.LeftInput())
        .RightLabel(join.LeftLabel())
        .JoinType().Build("RightSemi")
        .JoinKeys(joinKeysBuilder.Done())
        .Done();
}

TMaybeNode<TKqlKeyInc> GetRightTableKeyPrefix(const TKqlKeyRange& range) {
    if (!range.From().Maybe<TKqlKeyInc>() || !range.To().Maybe<TKqlKeyInc>()) {
        return {};
    }
    auto rangeFrom = range.From().Cast<TKqlKeyInc>();
    auto rangeTo = range.To().Cast<TKqlKeyInc>();

    if (rangeFrom.ArgCount() != rangeTo.ArgCount()) {
        return {};
    }
    for (ui32 i = 0; i < rangeFrom.ArgCount(); ++i) {
        if (rangeFrom.Arg(i).Raw() != rangeTo.Arg(i).Raw()) {
            return {};
        }
    }

    return rangeFrom;
}

TExprBase BuildLookupIndex(TExprContext& ctx, const TPositionHandle pos, const TKqlReadTableBase& read,
    const TExprBase& keysToLookup, const TVector<TCoAtom>& lookupNames, const TString& indexName)
{
    return Build<TKqlLookupIndex>(ctx, pos)
        .Table(read.Table())
        .LookupKeys<TCoSkipNullMembers>()
            .Input(keysToLookup)
            .Members()
                .Add(lookupNames)
                .Build()
            .Build()
        .Columns(read.Columns())
        .Index()
            .Build(indexName)
        .Done();
}

TExprBase BuildLookupTable(TExprContext& ctx, const TPositionHandle pos, const TKqlReadTableBase& read,
    const TExprBase& keysToLookup, const TVector<TCoAtom>& lookupNames)
{
    return Build<TKqlLookupTable>(ctx, pos)
        .Table(read.Table())
        .LookupKeys<TCoSkipNullMembers>()
            .Input(keysToLookup)
            .Members()
                .Add(lookupNames)
                .Build()
            .Build()
        .Columns(read.Columns())
        .Done();
}

TVector<TExprBase> CreateRenames(const TMaybeNode<TCoFlatMap>& rightFlatmap, const TCoAtomList& tableColumns,
    const TCoArgument& arg, const TStringBuf& rightLabel, TPositionHandle pos, TExprContext& ctx)
{
    TVector<TExprBase> renames;
    if (rightFlatmap) {
        const auto flatMapType = GetSeqItemType(rightFlatmap.Ref().GetTypeAnn());
        YQL_ENSURE(flatMapType->GetKind() == ETypeAnnotationKind::Struct);
        renames.reserve(flatMapType->Cast<TStructExprType>()->GetSize());

        for (const auto& column : flatMapType->Cast<TStructExprType>()->GetItems()) {
            renames.emplace_back(
                Build<TCoNameValueTuple>(ctx, pos)
                    .Name<TCoAtom>()
                        .Build(Join('.', rightLabel, column->GetName()))
                    .Value<TCoMember>()
                        .Struct(arg)
                        .Name<TCoAtom>()
                            .Build(column->GetName())
                        .Build()
                    .Done());
        }
    } else {
        renames.reserve(tableColumns.Size());

        for (const auto& column : tableColumns) {
            renames.emplace_back(
                Build<TCoNameValueTuple>(ctx, pos)
                    .Name<TCoAtom>()
                        .Build(Join('.', rightLabel, column.Value()))
                    .Value<TCoMember>()
                        .Struct(arg)
                        .Name(column)
                        .Build()
                    .Done());
        }
    }
    return renames;
}


//#define DBG(...) YQL_CLOG(DEBUG, ProviderKqp) << __VA_ARGS__
#define DBG(...)

TMaybeNode<TExprBase> KqpJoinToIndexLookupImpl(const TDqJoin& join, TExprContext& ctx, const TKqpOptimizeContext& kqpCtx) {
    if (!join.RightLabel().Maybe<TCoAtom>()) {
        // Lookup only in tables
        return {};
    }

    static THashSet<TStringBuf> supportedJoinKinds = {"Inner", "Left", "LeftOnly", "LeftSemi", "RightSemi"};
    if (!supportedJoinKinds.contains(join.JoinType().Value())) {
        return {};
    }

    TMaybeNode<TKqlReadTableBase> rightRead;
    TMaybeNode<TCoFlatMap> rightFlatmap;
    TMaybeNode<TCoFilterNullMembers> rightFilterNull;
    TMaybeNode<TCoSkipNullMembers> rightSkipNull;

    if (auto readTable = join.RightInput().Maybe<TKqlReadTableBase>()) {
        rightRead = readTable;
    }

    if (auto readTable = join.RightInput().Maybe<TCoFlatMap>().Input().Maybe<TKqlReadTableBase>()) {
        rightRead = readTable;
        rightFlatmap = join.RightInput().Maybe<TCoFlatMap>();
    }

    if (auto readTable = join.RightInput().Maybe<TCoFlatMap>().Input().Maybe<TCoFilterNullMembers>().Input().Maybe<TKqlReadTableBase>()) {
        rightRead = readTable;
        rightFlatmap = join.RightInput().Maybe<TCoFlatMap>();
        rightFilterNull = rightFlatmap.Input().Cast<TCoFilterNullMembers>();
    }

    if (auto readTable = join.RightInput().Maybe<TCoFlatMap>().Input().Maybe<TCoSkipNullMembers>().Input().Maybe<TKqlReadTableBase>()) {
        rightRead = readTable;
        rightFlatmap = join.RightInput().Maybe<TCoFlatMap>();
        rightSkipNull = rightFlatmap.Input().Cast<TCoSkipNullMembers>();
    }

    if (!rightRead) {
        return {};
    }

    Y_ENSURE(rightRead.Maybe<TKqlReadTable>() || rightRead.Maybe<TKqlReadTableIndex>());

    const TKqlReadTableBase read = rightRead.Cast();
    if (!read.Table().SysView().Value().empty()) {
        // Can't lookup in system views
        return {};
    }

    if (rightFlatmap && !IsPassthroughFlatMap(rightFlatmap.Cast(), nullptr)) {
        // Can't lookup in modified table
        return {};
    }

    auto maybeRightTableKeyPrefix = GetRightTableKeyPrefix(read.Range());
    if (!maybeRightTableKeyPrefix) {
        return {};
    }
    auto rightTableKeyPrefix = maybeRightTableKeyPrefix.Cast();

    TString lookupTable;
    TString indexName;

    if (auto indexRead = rightRead.Maybe<TKqlReadTableIndex>()) {
        indexName = indexRead.Cast().Index().StringValue();
        lookupTable = GetIndexMetadata(indexRead.Cast(), *kqpCtx.Tables, kqpCtx.Cluster)->Name;
    } else {
        lookupTable = read.Table().Path().StringValue();
    }

    const auto& rightTableDesc = kqpCtx.Tables->ExistingTable(kqpCtx.Cluster, lookupTable);

    TMap<std::string_view, TString> rightJoinKeyToLeft;
    TVector<TCoAtom> rightKeyColumns;
    rightKeyColumns.reserve(join.JoinKeys().Size());
    TSet<TString> leftJoinKeys;
    std::map<std::string_view, std::set<TString>> equalLeftKeys;

    for (ui32 i = 0; i < join.JoinKeys().Size(); ++i) {
        const auto& keyTuple = join.JoinKeys().Item(i);

        auto leftKey = join.LeftLabel().Maybe<TCoVoid>()
            ? Join('.', keyTuple.LeftLabel().Value(), keyTuple.LeftColumn().Value())
            : keyTuple.LeftColumn().StringValue();

        rightKeyColumns.emplace_back(keyTuple.RightColumn()); // unique elements

        auto [iter, newValue] = rightJoinKeyToLeft.emplace(keyTuple.RightColumn().Value(), leftKey);
        if (!newValue) {
            equalLeftKeys[iter->second].emplace(leftKey);
        }

        leftJoinKeys.emplace(leftKey);
    }

    auto leftRowArg = Build<TCoArgument>(ctx, join.Pos())
        .Name("leftRowArg")
        .Done();

    TVector<TExprBase> lookupMembers;
    TVector<TCoAtom> lookupNames;
    ui32 fixedPrefix = 0;
    for (auto& rightColumnName : rightTableDesc.Metadata->KeyColumnNames) {
        TExprNode::TPtr member;

        auto leftColumn = rightJoinKeyToLeft.FindPtr(rightColumnName);

        if (fixedPrefix < rightTableKeyPrefix.ArgCount()) {
            if (leftColumn) {
                return {};
            }

            member = rightTableKeyPrefix.Arg(fixedPrefix).Ptr();
            fixedPrefix++;
        } else {
            if (!leftColumn) {
                break;
            }

            member = Build<TCoMember>(ctx, join.Pos())
                .Struct(leftRowArg)
                .Name().Build(*leftColumn)
                .Done().Ptr();

            const TDataExprType* leftDataType;
            const TDataExprType* rightDataType;
            if (!GetEquiJoinKeyTypes(join.LeftInput(), *leftColumn, rightTableDesc, rightColumnName, leftDataType, rightDataType)) {
                return {};
            }

            if (leftDataType != rightDataType) {
                bool canCast = IsDataTypeNumeric(leftDataType->GetSlot()) && IsDataTypeNumeric(rightDataType->GetSlot());
                if (!canCast) {
                    canCast = leftDataType->GetName() == "Utf8" && rightDataType->GetName() == "String";
                }
                if (canCast) {
                    DBG("------ cast " << leftDataType->GetName() << " to " << rightDataType->GetName());
                    member = Build<TCoConvert>(ctx, join.Pos())
                        .Input(member)
                        .Type().Build(rightDataType->GetName())
                        .Done().Ptr();
                } else {
                    DBG("------ can not cast " << leftDataType->GetName() << " to " << rightDataType->GetName());
                    return {};
                }
            }
        }

        lookupMembers.emplace_back(
            Build<TExprList>(ctx, join.Pos())
                .Add<TCoAtom>().Build(rightColumnName)
                .Add(member)
                .Done());
        lookupNames.emplace_back(ctx.NewAtom(join.Pos(), rightColumnName));
    }

    if (lookupMembers.size() <= fixedPrefix) {
        return {};
    }

    auto leftData = Build<TDqPrecompute>(ctx, join.Pos())
        .Input(join.LeftInput())
        .Done();
    auto leftDataDeduplicated = DeduplicateByMembers(leftData, leftJoinKeys, ctx, join.Pos());

    if (!equalLeftKeys.empty())    {
        auto row = Build<TCoArgument>(ctx, join.Pos())
            .Name("row")
            .Done();

        TVector<TExprBase> conditions;

        for (auto [first, others]: equalLeftKeys) {
            auto v = Build<TCoMember>(ctx, join.Pos())
                .Struct(row)
                .Name().Build(first)
                .Done();

            for (std::string_view other: others) {
                conditions.emplace_back(
                    Build<TCoCmpEqual>(ctx, join.Pos())
                        .Left(v)
                        .Right<TCoMember>()
                            .Struct(row)
                            .Name().Build(other)
                            .Build()
                        .Done());
            }
        }

        leftDataDeduplicated = Build<TCoFilter>(ctx, join.Pos())
            .Input(leftDataDeduplicated)
            .Lambda()
                .Args({row})
                .Body<TCoCoalesce>()
                    .Predicate<TCoAnd>()
                        .Add(conditions)
                        .Build()
                    .Value<TCoBool>()
                        .Literal().Build("false")
                        .Build()
                    .Build()
                .Build()
            .Done();
    }

    auto keysToLookup = Build<TCoMap>(ctx, join.Pos())
        .Input(leftDataDeduplicated)
        .Lambda()
            .Args({leftRowArg})
            .Body<TCoAsStruct>()
                .Add(lookupMembers)
                .Build()
            .Build()
        .Done();

    TExprBase lookup = indexName
        ? BuildLookupIndex(ctx, join.Pos(), read, keysToLookup, lookupNames, indexName)
        : BuildLookupTable(ctx, join.Pos(), read, keysToLookup, lookupNames);

    // Skip null keys in lookup part as for equijoin semantics null != null,
    // so we can't have nulls in lookup part
    lookup = Build<TCoSkipNullMembers>(ctx, join.Pos())
        .Input(lookup)
        .Members()
            .Add(rightKeyColumns)
            .Build()
        .Done();

    if (rightFilterNull) {
        lookup = Build<TCoFilterNullMembers>(ctx, join.Pos())
            .Input(lookup)
            .Members(rightFilterNull.Cast().Members())
            .Done();
    }

    if (rightSkipNull) {
        lookup = Build<TCoSkipNullMembers>(ctx, join.Pos())
            .Input(lookup)
            .Members(rightSkipNull.Cast().Members())
            .Done();
    }

    if (rightFlatmap) {
        lookup = Build<TCoFlatMap>(ctx, join.Pos())
            .Input(lookup)
            .Lambda(rightFlatmap.Cast().Lambda())
            .Done();
    }

    if (join.JoinType().Value() == "RightSemi") {
        auto arg = TCoArgument(ctx.NewArgument(join.Pos(), "row"));
        auto rightLabel = join.RightLabel().Cast<TCoAtom>().Value();

        TVector<TExprBase> renames = CreateRenames(rightFlatmap, read.Columns(), arg, rightLabel, join.Pos(), ctx);

        lookup = Build<TCoMap>(ctx, join.Pos())
            .Input(lookup)
            .Lambda()
                .Args({arg})
                .Body<TCoAsStruct>()
                    .Add(renames)
                    .Build()
                .Build()
            .Done();

        return lookup;
    }

    return Build<TDqJoin>(ctx, join.Pos())
        .LeftInput(leftData)
        .LeftLabel(join.LeftLabel())
        .RightInput(lookup)
        .RightLabel(join.RightLabel())
        .JoinType(join.JoinType())
        .JoinKeys(join.JoinKeys())
        .Done();
}

} // anonymous namespace

TExprBase KqpJoinToIndexLookup(const TExprBase& node, TExprContext& ctx, const TKqpOptimizeContext& kqpCtx,
    const NYql::TKikimrConfiguration::TPtr& config)
{
    if (!kqpCtx.IsDataQuery() || !node.Maybe<TDqJoin>()) {
        return node;
    }
    auto join = node.Cast<TDqJoin>();

    DBG("-- Join: " << KqpExprToPrettyString(join, ctx));

    // SqlIn support (preferred lookup direction)
    if (join.JoinType().Value() == "LeftSemi" && !config->HasOptDisableJoinReverseTableLookupLeftSemi()) {
        auto flipJoin = FlipLeftSemiJoin(join, ctx);
        DBG("-- Flip join");

        if (auto indexLookupJoin = KqpJoinToIndexLookupImpl(flipJoin, ctx, kqpCtx)) {
            return indexLookupJoin.Cast();
        }
    }

    if (auto indexLookupJoin = KqpJoinToIndexLookupImpl(join, ctx, kqpCtx)) {
        return indexLookupJoin.Cast();
    }

    return node;
}

#undef DBG

} // namespace NKikimr::NKqp::NOpt
