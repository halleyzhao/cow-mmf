/**
 * Copyright (C) 2017 Alibaba Group Holding Limited. All Rights Reserved.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif

#include "multimedia/mm_debug.h"

MM_LOG_DEFINE_MODULE_NAME("MMDebugTest")
MM_LOG_DEFINE_LEVEL(MM_LOG_DEBUG)

const static char* logText[MM_LOG_LEVEL_COUNT] = {
    "testing mmdebug VERBOSE\n",
    "testing mmdebug DEBUG\n",
    "test mmdebug INFO \n",
    "test mmdebug WARN \n",
    "test mmdebug ERROR \n"
};

class MMDebugTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(MMDebugTest, logTest) {
    EXPECT_FALSE(mm_log_set_level((MMLogLevelType)10));//error level
    EXPECT_TRUE(mm_log_set_level(MM_LOG_ERROR));
    #ifndef MM_LOG_OUTPUT_V
    EXPECT_TRUE(mm_log_set_level(LOG_LEVEL));
    #else
    EXPECT_TRUE(mm_log_set_level(MM_LOG_VERBOSE));
    #endif
    int level = mm_log_get_level();
    EXPECT_LE(level, MM_LOG_VERBOSE);
    EXPECT_GE(level, MM_LOG_ERROR);
    for(level = MM_LOG_VERBOSE; level <= MM_LOG_ERROR; level++){
        int ret = mm_log((MMLogLevelType)level, MM_LOG_TAG, logText[level]);
        EXPECT_EQ(0, ret);
    }
    VERBOSE("testing mmdebug VERBOSE\n");
    DEBUG("testing mmdebug DEBUG\n");
    INFO("test mmdebug INFO \n");
    WARNING("test mmdebug WARN \n");
    ERROR("test mmdebug ERROR \n");
}


