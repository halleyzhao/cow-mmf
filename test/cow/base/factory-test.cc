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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <gtest/gtest.h>

#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"


MM_LOG_DEFINE_MODULE_NAME("factory-test");

using namespace YUNOS_MM;


class FactoryTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};
TEST_F(FactoryTest, factoryTest) {
      //test loading from component name
    DEBUG("factory test begin\n");
    {
        PRINTF("TEST loading from componet name\n");
        //check ref count
        ComponentSP vsbComponent1 = ComponentFactory::create("AVDemuxer", NULL, false);
        ASSERT_NE(vsbComponent1.get(), (Component*) NULL);
        PRINTF("vsbComponent1 getcount %ld %p\n", vsbComponent1.use_count(), vsbComponent1.get());
        ASSERT_TRUE(vsbComponent1.use_count() == 1);

        //ref count
        ComponentSP vsbComponent2 = ComponentFactory::create("AVDemuxer", NULL, false);
        PRINTF("vsbComponent2 getcount %ld %p\n", vsbComponent2.use_count(), vsbComponent2.get());
        ASSERT_NE(vsbComponent2.get(), NULL);
        ASSERT_TRUE(vsbComponent2.use_count() == 1);
        ASSERT_TRUE(vsbComponent1.get() != vsbComponent2.get());
        printf("TEST success\n\n\n");
    }

    //test loading from mime type
    {
        PRINTF("TEST loading from mime type\n");
        ComponentSP vsbComponent1 = ComponentFactory::create(NULL, MEDIA_MIMETYPE_MEDIA_DEMUXER, false);
        PRINTF("vsbComponent1 getcount %ld %p\n", vsbComponent1.use_count(), vsbComponent1.get());
        ASSERT_NE(vsbComponent1.get(), (Component*)NULL);
        ASSERT_TRUE(vsbComponent1.use_count() == 1);

        ComponentSP vsbComponent2 = ComponentFactory::create(NULL, MEDIA_MIMETYPE_MEDIA_DEMUXER, false);
        PRINTF("vsbComponent2 getcount %ld %p\n", vsbComponent2.use_count(), vsbComponent2.get());
        ASSERT_NE(vsbComponent2.get(), (Component*)NULL);
        ASSERT_TRUE(vsbComponent2.use_count() == 1);
        ASSERT_TRUE(vsbComponent1.get() != vsbComponent2.get());

        PRINTF("TEST success\n\n\n");
    }

    //take component name as priority
    {
        PRINTF("TEST loading from mime type and component name, take component name as priority\n");
        ComponentSP vsbComponent1 = ComponentFactory::create("AVDemuxer", MEDIA_MIMETYPE_MEDIA_MUXER, false);
        PRINTF("vsbComponent1 getcount %ld %p\n", vsbComponent1.use_count(), vsbComponent1.get());
        ASSERT_NE(vsbComponent1.get(), (Component*)NULL);
        ASSERT_TRUE(vsbComponent1.use_count() == 1);
        PRINTF("TEST success\n\n\n");
    }

}


