/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "velox/functions/prestosql/aggregates/SetAggregates.h"
#include "velox/functions/lib/aggregates/SetBaseAggregate.h"
#include "velox/functions/prestosql/aggregates/AggregateNames.h"

using namespace facebook::velox::functions::aggregate;

namespace facebook::velox::aggregate::prestosql {

namespace {

template <typename T, typename AccumulatorType = SetAccumulator<T>>
class SetUnionAggregate
    : public SetBaseAggregate<T, false, true, AccumulatorType> {
 public:
  using Base = SetBaseAggregate<T, false, true, AccumulatorType>;

  explicit SetUnionAggregate(const TypePtr& resultType) : Base(resultType) {}

  bool supportsToIntermediate() const override {
    return true;
  }

  void toIntermediate(
      const SelectivityVector& rows,
      std::vector<VectorPtr>& args,
      VectorPtr& result) const override {
    auto arrayInput = args[0];

    if (arrayInput->mayHaveNulls()) {
      // Convert null arrays into empty arrays. set_union(<all null>) returns
      // empty array, not null.

      auto copy = BaseVector::create<ArrayVector>(
          arrayInput->type(), rows.size(), arrayInput->pool());
      copy->copy(arrayInput.get(), rows, nullptr);

      rows.applyToSelected([&](auto row) {
        if (copy->isNullAt(row)) {
          copy->setOffsetAndSize(row, 0, 0);
          copy->setNull(row, false);
        }
      });

      arrayInput = copy;
    }

    if (rows.isAllSelected()) {
      result = arrayInput;
    } else {
      auto* pool = Base::allocator_->pool();
      const auto numRows = rows.size();

      // Set nulls for rows not present in 'rows'.
      BufferPtr nulls = allocateNulls(numRows, pool);
      memcpy(
          nulls->asMutable<uint64_t>(),
          rows.asRange().bits(),
          bits::nbytes(numRows));

      BufferPtr indices = allocateIndices(numRows, pool);
      auto* rawIndices = indices->asMutable<vector_size_t>();
      std::iota(rawIndices, rawIndices + numRows, 0);
      result =
          BaseVector::wrapInDictionary(nulls, indices, rows.size(), arrayInput);
    }
  }

  void addRawInput(
      char** groups,
      const SelectivityVector& rows,
      const std::vector<VectorPtr>& args,
      bool /*mayPushdown*/) override {
    // Make sure to clear null flag for the accumulators even if all inputs are
    // null. set_union(<all nulls>) returns empty array, not null.
    Base::addIntermediateResultsInt(groups, rows, args, true);
  }

  void addSingleGroupRawInput(
      char* group,
      const SelectivityVector& rows,
      const std::vector<VectorPtr>& args,
      bool /*mayPushdown*/) override {
    // Make sure to clear null flag for the accumulators even if all inputs are
    // null. set_union(<all nulls>) returns empty array, not null.
    Base::addSingleGroupIntermediateResultsInt(group, rows, args, true);
  }
};

/// Returns the number of distinct non-null values in a group. This is an
/// internal function only used for testing.
template <typename T, typename AccumulatorType = SetAccumulator<T>>
class CountDistinctAggregate
    : public SetAggAggregate<T, true, false, AccumulatorType> {
 public:
  using Base = SetAggAggregate<T, true, false, AccumulatorType>;

  explicit CountDistinctAggregate(
      const TypePtr& resultType,
      const TypePtr& inputType)
      : Base(resultType), inputType_{inputType} {}

  bool supportsToIntermediate() const override {
    return false;
  }

  void initializeNewGroups(
      char** groups,
      folly::Range<const vector_size_t*> indices) override {
    exec::Aggregate::setAllNulls(groups, indices);
    for (auto i : indices) {
      new (groups[i] + Base::offset_)
          AccumulatorType(inputType_, Base::allocator_);
    }
  }

  void extractAccumulators(char** groups, int32_t numGroups, VectorPtr* result)
      override {
    return Base::extractValues(groups, numGroups, result);
  }

  void extractValues(char** groups, int32_t numGroups, VectorPtr* result)
      override {
    auto flatResult = (*result)->as<FlatVector<int64_t>>();
    flatResult->resize(numGroups);

    uint64_t* rawNulls = exec::Aggregate::getRawNulls(flatResult);
    for (auto i = 0; i < numGroups; ++i) {
      auto* group = groups[i];
      if (Base::isNull(group)) {
        Base::clearNull(rawNulls, i);
        flatResult->set(i, 0);
      } else {
        Base::clearNull(rawNulls, i);

        const auto size = Base::value(group)->size();
        flatResult->set(i, size);
      }
    }
  }

 private:
  TypePtr inputType_;
};

template <
    template <typename T, typename AccumulatorType>
    class Aggregate,
    TypeKind Kind>
std::unique_ptr<exec::Aggregate> create(const TypePtr& resultType) {
  return std::make_unique<Aggregate<
      typename TypeTraits<Kind>::NativeType,
      aggregate::prestosql::CustomComparisonSetAccumulator<Kind>>>(resultType);
}

template <template <typename T, typename AcumulatorType = SetAccumulator<T>>
          class Aggregate>
std::unique_ptr<exec::Aggregate> create(
    const TypePtr& inputType,
    const TypePtr& resultType) {
  if (inputType->providesCustomComparison()) {
    return VELOX_DYNAMIC_SCALAR_TEMPLATE_TYPE_DISPATCH(
        create, Aggregate, inputType->kind(), resultType);
  }

  switch (inputType->kind()) {
    case TypeKind::BOOLEAN:
      return std::make_unique<Aggregate<bool>>(resultType);
    case TypeKind::TINYINT:
      return std::make_unique<Aggregate<int8_t>>(resultType);
    case TypeKind::SMALLINT:
      return std::make_unique<Aggregate<int16_t>>(resultType);
    case TypeKind::INTEGER:
      return std::make_unique<Aggregate<int32_t>>(resultType);
    case TypeKind::BIGINT:
      return std::make_unique<Aggregate<int64_t>>(resultType);
    case TypeKind::HUGEINT:
      VELOX_CHECK(
          inputType->isLongDecimal(),
          "Non-decimal use of HUGEINT is not supported");
      return std::make_unique<Aggregate<int128_t>>(resultType);
    case TypeKind::REAL:
      return std::make_unique<Aggregate<float>>(resultType);
    case TypeKind::DOUBLE:
      return std::make_unique<Aggregate<double>>(resultType);
    case TypeKind::TIMESTAMP:
      return std::make_unique<Aggregate<Timestamp>>(resultType);
    case TypeKind::VARBINARY:
      [[fallthrough]];
    case TypeKind::VARCHAR:
      return std::make_unique<Aggregate<StringView>>(resultType);
    case TypeKind::ARRAY:
      [[fallthrough]];
    case TypeKind::MAP:
      [[fallthrough]];
    case TypeKind::ROW:
      return std::make_unique<Aggregate<ComplexType>>(resultType);
    case TypeKind::UNKNOWN:
      return std::make_unique<Aggregate<UnknownValue>>(resultType);
    default:
      VELOX_UNREACHABLE(
          "Unexpected type {}", mapTypeKindToName(inputType->kind()));
  }
}

template <TypeKind Kind>
std::unique_ptr<exec::Aggregate> creatSetAggAggregate(
    const TypePtr& resultType) {
  return std::make_unique<SetAggAggregate<
      typename TypeTraits<Kind>::NativeType,
      false,
      true,
      aggregate::prestosql::CustomComparisonSetAccumulator<Kind>>>(resultType);
}

template <TypeKind Kind>
std::unique_ptr<exec::Aggregate> createCountDistinctAggregate(
    const TypePtr& resultType,
    const TypePtr& inputType) {
  return std::make_unique<CountDistinctAggregate<
      typename TypeTraits<Kind>::NativeType,
      aggregate::prestosql::CustomComparisonSetAccumulator<Kind>>>(
      resultType, inputType);
}

} // namespace

void registerSetAggAggregate(
    const std::string& prefix,
    bool withCompanionFunctions,
    bool overwrite) {
  std::vector<std::shared_ptr<exec::AggregateFunctionSignature>> signatures = {
      exec::AggregateFunctionSignatureBuilder()
          .typeVariable("T")
          .returnType("array(T)")
          .intermediateType("array(T)")
          .argumentType("T")
          .build()};

  auto name = prefix + kSetAgg;
  exec::registerAggregateFunction(
      name,
      std::move(signatures),
      [name](
          core::AggregationNode::Step step,
          const std::vector<TypePtr>& argTypes,
          const TypePtr& resultType,
          const core::QueryConfig& /*config*/)
          -> std::unique_ptr<exec::Aggregate> {
        VELOX_CHECK_EQ(argTypes.size(), 1);

        const bool isRawInput = exec::isRawInput(step);
        const TypePtr& inputType =
            isRawInput ? argTypes[0] : argTypes[0]->childAt(0);
        const TypeKind typeKind = inputType->kind();

        if (inputType->providesCustomComparison()) {
          return VELOX_DYNAMIC_SCALAR_TYPE_DISPATCH(
              creatSetAggAggregate, inputType->kind(), resultType);
        }

        switch (typeKind) {
          case TypeKind::BOOLEAN:
            return std::make_unique<SetAggAggregate<bool>>(resultType);
          case TypeKind::TINYINT:
            return std::make_unique<SetAggAggregate<int8_t>>(resultType);
          case TypeKind::SMALLINT:
            return std::make_unique<SetAggAggregate<int16_t>>(resultType);
          case TypeKind::INTEGER:
            return std::make_unique<SetAggAggregate<int32_t>>(resultType);
          case TypeKind::BIGINT:
            return std::make_unique<SetAggAggregate<int64_t>>(resultType);
          case TypeKind::HUGEINT:
            VELOX_CHECK(
                inputType->isLongDecimal(),
                "Non-decimal use of HUGEINT is not supported");
            return std::make_unique<SetAggAggregate<int128_t>>(resultType);
          case TypeKind::REAL:
            return std::make_unique<SetAggAggregate<float>>(resultType);
          case TypeKind::DOUBLE:
            return std::make_unique<SetAggAggregate<double>>(resultType);
          case TypeKind::TIMESTAMP:
            return std::make_unique<SetAggAggregate<Timestamp>>(resultType);
          case TypeKind::VARBINARY:
            [[fallthrough]];
          case TypeKind::VARCHAR:
            return std::make_unique<SetAggAggregate<StringView>>(resultType);
          case TypeKind::ARRAY:
            [[fallthrough]];
          case TypeKind::MAP:
            [[fallthrough]];
          case TypeKind::ROW:
            return std::make_unique<SetAggAggregate<ComplexType>>(resultType);
          case TypeKind::UNKNOWN:
            return std::make_unique<SetAggAggregate<UnknownValue>>(resultType);
          default:
            VELOX_UNREACHABLE(
                "Unexpected type {}", mapTypeKindToName(typeKind));
        }
      },
      withCompanionFunctions,
      overwrite);
}

void registerSetUnionAggregate(
    const std::string& prefix,
    bool withCompanionFunctions,
    bool overwrite) {
  std::vector<std::shared_ptr<exec::AggregateFunctionSignature>> signatures = {
      exec::AggregateFunctionSignatureBuilder()
          .typeVariable("T")
          .returnType("array(T)")
          .intermediateType("array(T)")
          .argumentType("array(T)")
          .build()};

  auto name = prefix + kSetUnion;
  exec::registerAggregateFunction(
      name,
      std::move(signatures),
      [name](
          core::AggregationNode::Step /*step*/,
          const std::vector<TypePtr>& argTypes,
          const TypePtr& resultType,
          const core::QueryConfig& /*config*/)
          -> std::unique_ptr<exec::Aggregate> {
        VELOX_CHECK_EQ(argTypes.size(), 1);

        return create<SetUnionAggregate>(argTypes[0]->childAt(0), resultType);
      },
      withCompanionFunctions,
      overwrite);
}

void registerCountDistinctAggregate(
    const std::string& prefix,
    bool withCompanionFunctions,
    bool overwrite) {
  std::vector<std::shared_ptr<exec::AggregateFunctionSignature>> signatures = {
      exec::AggregateFunctionSignatureBuilder()
          .typeVariable("T")
          .returnType("bigint")
          .intermediateType("array(T)")
          .argumentType("T")
          .build()};

  auto name = prefix + "$internal$count_distinct";
  exec::registerAggregateFunction(
      name,
      std::move(signatures),
      [](core::AggregationNode::Step step,
         const std::vector<TypePtr>& argTypes,
         const TypePtr& resultType,
         const core::QueryConfig& /*config*/)
          -> std::unique_ptr<exec::Aggregate> {
        VELOX_CHECK_EQ(argTypes.size(), 1);

        const bool isRawInput = exec::isRawInput(step);
        const TypePtr& inputType =
            isRawInput ? argTypes[0] : argTypes[0]->childAt(0);
        const TypeKind typeKind = inputType->kind();

        if (inputType->providesCustomComparison()) {
          return VELOX_DYNAMIC_SCALAR_TYPE_DISPATCH(
              createCountDistinctAggregate,
              inputType->kind(),
              resultType,
              inputType);
        }

        switch (typeKind) {
          case TypeKind::BOOLEAN:
            return std::make_unique<CountDistinctAggregate<bool>>(
                resultType, argTypes[0]);
          case TypeKind::TINYINT:
            return std::make_unique<CountDistinctAggregate<int8_t>>(
                resultType, argTypes[0]);
          case TypeKind::SMALLINT:
            return std::make_unique<CountDistinctAggregate<int16_t>>(
                resultType, argTypes[0]);
          case TypeKind::INTEGER:
            return std::make_unique<CountDistinctAggregate<int32_t>>(
                resultType, argTypes[0]);
          case TypeKind::BIGINT:
            return std::make_unique<CountDistinctAggregate<int64_t>>(
                resultType, argTypes[0]);
          case TypeKind::HUGEINT:
            return std::make_unique<CountDistinctAggregate<int128_t>>(
                resultType, argTypes[0]);
          case TypeKind::REAL:
            return std::make_unique<CountDistinctAggregate<float>>(
                resultType, argTypes[0]);
          case TypeKind::DOUBLE:
            return std::make_unique<CountDistinctAggregate<double>>(
                resultType, argTypes[0]);
          case TypeKind::TIMESTAMP:
            return std::make_unique<CountDistinctAggregate<Timestamp>>(
                resultType, argTypes[0]);
          case TypeKind::VARBINARY:
            [[fallthrough]];
          case TypeKind::VARCHAR:
            return std::make_unique<CountDistinctAggregate<StringView>>(
                resultType, argTypes[0]);
          case TypeKind::ARRAY:
            [[fallthrough]];
          case TypeKind::MAP:
            [[fallthrough]];
          case TypeKind::ROW:
            return std::make_unique<CountDistinctAggregate<ComplexType>>(
                resultType, argTypes[0]);
          case TypeKind::UNKNOWN:
            return std::make_unique<CountDistinctAggregate<UnknownValue>>(
                resultType, argTypes[0]);
          default:
            VELOX_UNREACHABLE(
                "Unexpected type {}", mapTypeKindToName(typeKind));
        }
      },
      withCompanionFunctions,
      overwrite);
}

} // namespace facebook::velox::aggregate::prestosql
