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

#include "multimedia/mmmsgthread.h"

#include "multimedia/mm_debug.h"

MM_LOG_DEFINE_MODULE_NAME("MMMSGTST")

using namespace YUNOS_MM;
static const char * MMMSGTHREAD_NAME = "MsgTest";

#define TEST_MESSAGE_1 (msg_type)1
#define TEST_MESSAGE_2 (msg_type)2
#define TEST_MESSAGE_3 (msg_type)3
#define TEST_MESSAGE_4 (msg_type)4
#define TEST_MESSAGE_5 (msg_type)5

class MMThreadTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

class TestParam : public MMRefBase {
public:
    TestParam(int i) : mI(i) {}
    ~TestParam() { MMLOGI("+\n"); }
    int mI;
};
typedef MMSharedPtr<TestParam> TestParamSP;

class TestParam2 : public MMRefBase {
public:
    TestParam2(int i) : mI(i) {}
    ~TestParam2() { MMLOGI("+\n"); }
    int mI;
};
typedef MMSharedPtr<TestParam2> TestParam2SP;

class MsgTest : public MMMsgThread {
public:
    MsgTest(): MMMsgThread(MMMSGTHREAD_NAME)
    {
    }
    ~MsgTest()
    {
        MMLOGI("+\n");
    }

public:
    int post_test1()
    {
        MMLOGD("posting msg\n");
        postMsg(TEST_MESSAGE_1, 101, 0);
        MMLOGD("posting msg over\n");
        return 0;
    }
    int post_test2()
    {
        char * s = new char[32];
        strcpy(s, "hello");
        MMLOGD("posting msg\n");
        postMsg(TEST_MESSAGE_2, 201, s);
        MMLOGD("posting msg over\n");
        return 0;
    }

    int send_test1()
    {
        MMLOGD("sending msg\n");
        sendMsg(TEST_MESSAGE_3, 301, 0, 0, 0);
        MMLOGD("sending msg over\n");
        return 0;
    }
    int send_test2()
    {
        MMLOGD("sending msg\n");
        char * sp = new char[32];
        strcpy(sp, "good");
        param1_type rsp_param1;
        param2_type rsp_param2;
        sendMsg(TEST_MESSAGE_4, 401, sp, &rsp_param1, &rsp_param2);
        MMLOGD("sending msg over, rsp param1: %u, param2: %p\n", rsp_param1, rsp_param2);
        char * s = (char*)rsp_param2;
        MMLOGD("param2: %s", s);
        delete []s;
        return 0;
    }

    int send_test3()
    {
        MMLOGD("sending msg\n");
        param1_type rsp_param1;
        param2_type rsp_param2;
        TestParamSP param3(new TestParam(10));
        param3_type resp_param3;
        sendMsg(TEST_MESSAGE_5, 402, 0, param3, &rsp_param1, &rsp_param2, &resp_param3);
        TestParam2SP rsp_3 = std::tr1::dynamic_pointer_cast<TestParam2>(resp_param3);
        EXPECT_NE(rsp_3.get(), NULL);
        EXPECT_EQ(rsp_3->mI, 100);
        MMLOGD("sending msg over, rsp param1: %u, param2: %p\n", rsp_param1, rsp_param2);
        char * s = (char*)rsp_param2;
        MMLOGD("param2: %s", s);
        delete []s;
        return 0;
    }

private:
    void onMessage1(param1_type param1, param2_type param2, uint32_t rspId)
    {
        MMLOGD(">>>param1: %u, param2: %p, rspId: %u\n", param1, param2, rspId);
        EXPECT_EQ(rspId, 0);
        MMLOGD(">>>\n");
    }

    void onMessage2(param1_type param1, param2_type param2, uint32_t rspId)
    {
        MMLOGD(">>>param1: %u, param2: %p, rspId: %u\n", param1, param2, rspId);
        EXPECT_EQ(rspId, 0);
        char * s = (char*)param2;
        MMLOGD("param2: %s\n", s);
        delete []s;
        MMLOGD(">>>\n");
    }

    void onMessage3(param1_type param1, param2_type param2, uint32_t rspId)
    {
        MMLOGD(">>>param1: %u, param2: %p, rspId: %u\n", param1, param2, rspId);
        EXPECT_NE(rspId, 0);
        char * s = (char*)param2;
        MMLOGD("param2: %s\n", s);
        delete []s;
        postReponse(rspId, 0, 0);
        MMLOGD(">>>\n");
    }

    void onMessage4(param1_type param1, param2_type param2, uint32_t rspId)
    {
        MMLOGD(">>>param1: %u, param2: %p, rspId: %u\n", param1, param2, rspId);
        EXPECT_NE(rspId, 0);
        char * s = (char*)param2;
        MMLOGD("param2: %s\n", s);
        delete []s;
        char * ss = new char[32];
        strcpy(ss, "reply good");
        postReponse(rspId, 402, ss);
        MMLOGD(">>>\n");
    }

    void onMessage5(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
    {
        MMLOGD(">>>param1: %u, param2: %p, rspId: %u\n", param1, param2, rspId);
        EXPECT_NE(rspId, 0);
        TestParamSP p3 = std::tr1::dynamic_pointer_cast<TestParam>(param3);
        EXPECT_NE(p3.get(), NULL);
        EXPECT_EQ(p3->mI, 10);
        param3_type rsp3(new  TestParam2(100));
        postReponse(rspId, 402, 0, rsp3);
        MMLOGD(">>>\n");
    }


    DECLARE_MSG_LOOP()
};

BEGIN_MSG_LOOP(MsgTest)
    MSG_ITEM(TEST_MESSAGE_1, onMessage1)
    MSG_ITEM(TEST_MESSAGE_2, onMessage2)
    MSG_ITEM(TEST_MESSAGE_3, onMessage3)
    MSG_ITEM(TEST_MESSAGE_4, onMessage4)
    MSG_ITEM2(TEST_MESSAGE_5, onMessage5)
END_MSG_LOOP()

TEST_F(MMThreadTest, PostTest) {
        MsgTest test;
        test.run();
        EXPECT_EQ(test.post_test1(), 0);
        test.exit();
}


TEST_F(MMThreadTest, PostSendTest) {
        MsgTest test;
        test.run();
        EXPECT_EQ(test.post_test2(), 0);
        EXPECT_EQ(test.send_test1(), 0);
        test.exit();
}

TEST_F(MMThreadTest, sendWithStringParamTest) {
        MsgTest test;
        test.run();
        EXPECT_EQ(test.send_test2(), 0);
        test.exit();
}

TEST_F(MMThreadTest, sendWithObjParamTest) {
        MsgTest test;
        test.run();
        EXPECT_EQ(test.send_test3(), 0);
        test.exit();
}

int main(int argc, char* const argv[]) {
  int ret;
  MMLOGD("testing message\n");
  try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
   } catch (testing::internal::GoogleTestFailureException) {
        MMLOGE("InitGoogleTest failed!");
        return -1;
   } catch (...) {
        MMLOGE("unknown exception!");
        return -1;
   }
  usleep(3000000);
  MMLOGD("testing message over\n");
  MMLOGD("exit\n");
  return ret;
}

