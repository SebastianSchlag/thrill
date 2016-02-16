/*******************************************************************************
 * tests/core/reduce_pre_stage_test.cpp
 *
 * Part of Project Thrill - http://project-thrill.org
 *
 * Copyright (C) 2015 Matthias Stumpp <mstumpp@gmail.com>
 * Copyright (C) 2016 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the BSD-2 license in the LICENSE file.
 ******************************************************************************/

#include <thrill/core/reduce_pre_stage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

using namespace thrill;

struct MyStruct
{
    size_t key, value;

    bool operator < (const MyStruct& b) const { return key < b.key; }

    friend std::ostream& operator << (std::ostream& os, const MyStruct& c) {
        return os << '(' << c.key << ',' << c.value << ')';
    }
};

/******************************************************************************/

template <
    template <
        typename ValueType, typename Key, typename Value,
        typename KeyExtractor, typename ReduceFunction,
        const bool RobustKey,
        typename IndexFunction = core::ReduceByHash<Key>,
        typename ReduceStageConfig = core::DefaultReduceTableConfig,
        typename EqualToFunction = std::equal_to<Key> >
    class PreStage>
static void TestAddMyStructByHash(Context& ctx) {
    static const size_t mod_size = 601;
    static const size_t test_size = mod_size * 100;

    auto key_ex = [](const MyStruct& in) {
                      return in.key % mod_size;
                  };

    auto red_fn = [](const MyStruct& in1, const MyStruct& in2) {
                      return MyStruct {
                                 in1.key, in1.value + in2.value
                      };
                  };

    // collect all items
    const size_t num_partitions = 13;

    std::vector<data::File> files;
    for (size_t i = 0; i < num_partitions; ++i)
        files.emplace_back(ctx.GetFile());

    std::vector<data::DynBlockWriter> emitters;
    for (size_t i = 0; i < num_partitions; ++i)
        emitters.emplace_back(files[i].GetDynWriter());

    // process items with stage
    using Stage = PreStage<
              MyStruct, size_t, MyStruct,
              decltype(key_ex), decltype(red_fn), true>;

    core::DefaultReduceTableConfig config;
    config.limit_memory_bytes_ = 1024 * 1024;

    Stage stage(ctx,
                num_partitions,
                key_ex, red_fn, emitters,
                core::ReduceByHash<size_t>(),
                /* sentinel */ size_t(-1),
                config);

    stage.Initialize();

    for (size_t i = 0; i < test_size; ++i) {
        stage.Insert(MyStruct { i, i / mod_size });
    }

    stage.FlushAll();
    stage.CloseAll();

    // collect items and check result
    std::vector<MyStruct> result;

    for (size_t i = 0; i < num_partitions; ++i) {
        data::File::Reader r = files[i].GetReader(/* consume */ true);
        while (r.HasNext())
            result.emplace_back(r.Next<MyStruct>());
    }

    std::sort(result.begin(), result.end());

    ASSERT_EQ(mod_size, result.size());

    for (size_t i = 0; i < result.size(); ++i) {
        ASSERT_EQ(i, result[i].key);
        ASSERT_EQ((test_size / mod_size) * ((test_size / mod_size) - 1) / 2,
                  result[i].value);
    }
}

TEST(ReducePreStage, BucketAddMyStructByHash) {
    api::RunLocalSameThread(
        [](Context& ctx) {
            TestAddMyStructByHash<core::ReducePreBucketStage>(ctx);
        });
}

TEST(ReducePreStage, ProbingAddMyStructByHash) {
    api::RunLocalSameThread(
        [](Context& ctx) {
            TestAddMyStructByHash<core::ReducePreProbingStage>(ctx);
        });
}

/******************************************************************************/

template <
    template <typename ValueType, typename Key, typename Value,
              typename KeyExtractor, typename ReduceFunction,
              const bool SendPair = false,
              typename IndexFunction = core::ReduceByIndex<Key>,
              typename ReduceStageConfig = core::DefaultReduceTableConfig,
              typename EqualToFunction = std::equal_to<Key> >
    class PreStage>
static void TestAddMyStructByIndex(Context& ctx) {
    static const size_t mod_size = 601;
    static const size_t test_size = mod_size * 100;

    auto key_ex = [](const MyStruct& in) {
                      return in.key % mod_size;
                  };

    auto red_fn = [](const MyStruct& in1, const MyStruct& in2) {
                      return MyStruct {
                                 in1.key, in1.value + in2.value
                      };
                  };

    // collect all items
    const size_t num_partitions = 13;

    std::vector<data::File> files;
    for (size_t i = 0; i < num_partitions; ++i)
        files.emplace_back(ctx.GetFile());

    std::vector<data::DynBlockWriter> emitters;
    for (size_t i = 0; i < num_partitions; ++i)
        emitters.emplace_back(files[i].GetDynWriter());

    // process items with stage
    using Stage = PreStage<
              MyStruct, size_t, MyStruct,
              decltype(key_ex), decltype(red_fn), true,
              core::ReduceByIndex<size_t> >;

    core::DefaultReduceTableConfig config;
    config.limit_memory_bytes_ = 1024 * 1024;

    Stage stage(ctx,
                num_partitions,
                key_ex, red_fn, emitters,
                core::ReduceByIndex<size_t>(0, mod_size),
                /* sentinel */ size_t(-1),
                config);

    stage.Initialize();

    for (size_t i = 0; i < test_size; ++i) {
        stage.Insert(MyStruct { i, i / mod_size });
    }

    stage.FlushAll();
    stage.CloseAll();

    // collect items and check result - they must be in correct order!
    std::vector<MyStruct> result;

    for (size_t i = 0; i < num_partitions; ++i) {
        data::File::Reader r = files[i].GetReader(/* consume */ true);
        while (r.HasNext())
            result.emplace_back(r.Next<MyStruct>());
    }

    ASSERT_EQ(mod_size, result.size());

    for (size_t i = 0; i < result.size(); ++i) {
        ASSERT_EQ(i, result[i].key);
        ASSERT_EQ((test_size / mod_size) * ((test_size / mod_size) - 1) / 2,
                  result[i].value);
    }
}

TEST(ReducePreStage, BucketAddMyStructByIndex) {
    api::RunLocalSameThread(
        [](Context& ctx) {
            TestAddMyStructByIndex<core::ReducePreBucketStage>(ctx);
        });
}

TEST(ReducePreStage, ProbingAddMyStructByIndex) {
    api::RunLocalSameThread(
        [](Context& ctx) {
            TestAddMyStructByIndex<core::ReducePreProbingStage>(ctx);
        });
}

/******************************************************************************/
