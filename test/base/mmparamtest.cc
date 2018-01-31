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
#include <stdlib.h>

#include "multimedia/mmparam.h"
#include "multimedia/mm_debug.h"

MM_LOG_DEFINE_MODULE_NAME("MMParamTest")

using namespace YUNOS_MM;

MMParam g_param;
class MMParamTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};
struct TestS : public MMRefBase {
    TestS(int i) : mI(i) {}
    ~TestS() { MMLOGI("+\n");}
    int mI;
    virtual void f(){}
};
typedef MMSharedPtr<TestS> TestSSP;

struct TestS2 : public MMRefBase {
    TestS2(int i) : mI(i) {}
    ~TestS2() { MMLOGI("+\n");}
    int mI;
    virtual void f(){}
};
typedef MMSharedPtr<TestS2> TestS2SP;

static void addPointer(MMParam & param){
    TestSSP pointer(new TestS(1));
    ASSERT_NE(NULL, pointer.get());
    TestS2SP pointer1(new TestS2(2));
    ASSERT_NE(NULL, pointer1.get());
    EXPECT_EQ(param.writePointer(pointer),MM_ERROR_SUCCESS);
    EXPECT_EQ(param.writePointer(pointer1),MM_ERROR_SUCCESS);
}


static void verifyPointer(const MMParam & param){
    TestSSP r = std::tr1::dynamic_pointer_cast<TestS>(param.readPointer());
    ASSERT_NE(r.get(), NULL);
    MMLOGI("gotp1: %d\n", r->mI);
    TestS2SP r2 = std::tr1::dynamic_pointer_cast<TestS2>(param.readPointer());
    ASSERT_NE(r2.get(), NULL);
    MMLOGI("gotp2: %d\n", r2->mI);
}


TEST_F(MMParamTest, paramWriteTest) {
    MMLOGD("testing MMParam write\n");
    EXPECT_EQ(g_param.writeInt32(12),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeInt32(34),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeInt64(56),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeInt64(78),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeFloat(0.11),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeFloat(0.22),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeDouble(0.33),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeDouble(0.44),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeCString("hello"),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeCString("world"),MM_ERROR_SUCCESS);
    EXPECT_EQ(g_param.writeInt32(0xef),MM_ERROR_SUCCESS);
    addPointer(g_param);
}

TEST_F(MMParamTest, paramReadTest) {
    MMLOGD("testing MMParam read\n");
    int32_t i;
    g_param.readInt32(&i);
    EXPECT_EQ(i, 12);
    MMLOGI("retrieve: %d\n", i);
    g_param.readInt32(&i);
    EXPECT_EQ(i, 34);
    MMLOGI("retrieve: %d\n", i);

    int64_t j;
    g_param.readInt64(&j);
    EXPECT_EQ(j, 56);
    MMLOGI("retrieve: %" PRId64 "\n", j);
    g_param.readInt64(&j);
    EXPECT_EQ(j, 78);
    MMLOGI("retrieve: %" PRId64 "\n", j);

    float f;
    g_param.readFloat(&f);
    EXPECT_EQ(f, ( float )0.11);
    MMLOGI("retrieve: %f\n", f);
    g_param.readFloat(&f);
    EXPECT_EQ(f, ( float )0.22);
    MMLOGI("retrieve: %f\n", f);

    double d;
    g_param.readDouble(&d);
    EXPECT_EQ(d, 0.33);
    MMLOGI("retrieve: %f\n", d);
    g_param.readDouble(&d);
    EXPECT_EQ(d, 0.44);
    MMLOGI("retrieve: %f\n", d);

    const char * s;
    s = g_param.readCString();
    EXPECT_STREQ(s, "hello");
    MMLOGI("retrieve: %s\n", s);
    s = g_param.readCString();
    EXPECT_STREQ(s, "world");
    MMLOGI("retrieve: %s\n", s);

    g_param.readInt32(&i);
    EXPECT_EQ(i, 0xef);
    MMLOGI("retrieve: %d\n", i);

    verifyPointer(g_param);

}

TEST_F(MMParamTest, paramCopyTest) {
    MMLOGI("testing MMParam copy \n");
    MMParam param_copy1 = MMParam(g_param);
    MMParam param_copy2 = MMParam(&g_param);

    int32_t i_cp = 0;
    param_copy1.readInt32(&i_cp);
    EXPECT_EQ(i_cp, 12);
    MMLOGI("retrieve copy 1: %d\n", i_cp);
    param_copy2.readInt32(&i_cp);
    EXPECT_EQ(i_cp, 12);
    MMLOGI("retrieve copy 2: %d\n", i_cp);
    param_copy1.readInt32(&i_cp);
    EXPECT_EQ(i_cp, 34);
    MMLOGI("retrieve copy 1: %d\n", i_cp);
    param_copy2.readInt32(&i_cp);
    EXPECT_EQ(i_cp, 34);
    MMLOGI("retrieve copy 2: %d\n", i_cp);

    int64_t j_cp =0;
    param_copy1.readInt64(&j_cp);
    EXPECT_EQ(j_cp ,56);
    MMLOGI("retrieve copy 1: %" PRId64 "\n", j_cp);
    param_copy2.readInt64(&j_cp);
    EXPECT_EQ(j_cp, 56);
    MMLOGI("retrieve copy 2: %" PRId64 "\n", j_cp);
    param_copy1.readInt64(&j_cp);
    EXPECT_EQ(j_cp, 78);
    MMLOGI("retrieve copy 1: %" PRId64 "\n", j_cp);
    param_copy2.readInt64(&j_cp);
    EXPECT_EQ(j_cp, 78);
    MMLOGI("retrieve copy 2: %" PRId64 "\n", j_cp);

    float f_cp = 0;
    param_copy1.readFloat(&f_cp);
    EXPECT_EQ(f_cp, ( float )0.11);
    MMLOGI("retrieve copy 1: %f\n", f_cp);
    param_copy2.readFloat(&f_cp);
    EXPECT_EQ(f_cp, ( float )0.11);
    MMLOGI("retrieve copy 2: %f\n", f_cp);
    param_copy1.readFloat(&f_cp);
    EXPECT_EQ(f_cp, ( float )0.22);
    MMLOGI("retrieve copy 1: %f\n", f_cp);
    param_copy2.readFloat(&f_cp);
    EXPECT_EQ(f_cp, ( float )0.22);
    MMLOGI("retrieve copy 2: %f\n", f_cp);

    double d_cp = 0;
    param_copy1.readDouble(&d_cp);
    EXPECT_EQ(d_cp, 0.33);
    MMLOGI("retrieve copy 1: %f\n", d_cp);
    param_copy2.readDouble(&d_cp);
    EXPECT_EQ(d_cp, 0.33);
    MMLOGI("retrieve copy 2: %f\n", d_cp);
    param_copy1.readDouble(&d_cp);
    EXPECT_EQ(d_cp, 0.44);
    MMLOGI("retrieve copy 1: %f\n", d_cp);
    param_copy2.readDouble(&d_cp);
    EXPECT_EQ(d_cp, 0.44);
    MMLOGI("retrieve copy 2: %f\n", d_cp);

    const char * s_cp;
    s_cp = param_copy1.readCString();
    EXPECT_STREQ(s_cp,"hello");
    MMLOGI("retrieve copy 1: %s\n", s_cp);
    s_cp = param_copy2.readCString();
    EXPECT_STREQ(s_cp, "hello");
    MMLOGI("retrieve copy 2: %s\n", s_cp);
    s_cp = param_copy1.readCString();
    EXPECT_STREQ(s_cp , "world");
    MMLOGI("retrieve copy 1: %s\n", s_cp);
    s_cp = param_copy2.readCString();
    EXPECT_STREQ(s_cp, "world");
    MMLOGI("retrieve copy 2: %s\n", s_cp);

    param_copy1.readInt32(&i_cp);
    EXPECT_EQ(i_cp, 0xef);
    MMLOGI("retrieve copy 1: %d\n", i_cp);
    param_copy2.readInt32(&i_cp);
    EXPECT_EQ(i_cp, 0xef);
    MMLOGI("retrieve copy 2: %d\n", i_cp);

    verifyPointer(param_copy1);
    verifyPointer(param_copy2);
}

TEST_F(MMParamTest, paramOpCopyTest) {
    MMLOGI("testing MMParam operator copy\n");
    MMParam param_copy3 = g_param;

    int32_t i_cp = 0;
    i_cp = param_copy3.readInt32();
    EXPECT_EQ(i_cp, 12);
    MMLOGI("retrieve copy 3: %d\n", i_cp);
    i_cp = param_copy3.readInt32();
    EXPECT_EQ(i_cp, 34);
    MMLOGI("retrieve copy 3: %d\n", i_cp);

    int64_t j_cp =0;
    j_cp = param_copy3.readInt64();
    EXPECT_EQ(j_cp, 56);
    MMLOGI("retrieve copy 3: %" PRId64 "\n", j_cp);
    j_cp = param_copy3.readInt64();
    EXPECT_EQ(j_cp, 78);
    MMLOGI("retrieve copy 3: %" PRId64 "\n", j_cp);

    float f_cp = 0;
    f_cp = param_copy3.readFloat();
    EXPECT_EQ(f_cp, ( float )0.11);
    MMLOGI("retrieve copy 3: %f\n", f_cp);
    f_cp = param_copy3.readFloat();
    EXPECT_EQ(f_cp, ( float )0.22);
    MMLOGI("retrieve copy 3: %f\n", f_cp);

    double d_cp = 0;
    d_cp = param_copy3.readDouble();
    EXPECT_EQ(d_cp, 0.33);
    MMLOGI("retrieve copy 3: %f\n", d_cp);
    d_cp = param_copy3.readDouble();
    EXPECT_EQ(d_cp, 0.44);
    MMLOGI("retrieve copy 3: %f\n", d_cp);

    const char * s_cp;
    s_cp = param_copy3.readCString();
    EXPECT_STREQ(s_cp, "hello");
    MMLOGI("retrieve copy 3: %s\n", s_cp);
    s_cp = param_copy3.readCString();
    EXPECT_STREQ(s_cp, "world");
    MMLOGI("retrieve copy 3: %s\n", s_cp);

    i_cp = param_copy3.readInt32();
    EXPECT_EQ(i_cp, 0xef);
    MMLOGI("retrieve copy 3: %d\n", i_cp);

    verifyPointer(param_copy3);
}


int main(int argc, char* const argv[]) {
    int ret;
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
     } catch (...) {
        MMLOGE("InitGoogleTest failed!");
        return -1;
    }
    return ret;
}


