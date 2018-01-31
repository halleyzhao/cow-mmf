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

#include <MediaClientHelper.h>
#include <string/String.h>

using namespace YUNOS_MM;

int main(int argc, char** argv) {

    FILE *fp = freopen("/dev/null", "w", stderr);

    String string = MediaClientHelper::dumpsys();
    printf("%s", string.c_str());

    if (!fp)
        fclose(fp);

    return 0;

}
