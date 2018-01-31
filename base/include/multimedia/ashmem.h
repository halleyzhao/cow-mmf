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

#ifndef __MM_ASHMEM_BASE_H__
#define __MM_ASHMEM_BASE_H__

struct asm_pin {
  unsigned int offset;
  unsigned int  len;
};

#define ASM_NAME "/dev/ashmem"
#define ASM_NAME_LEN 256
#define ASM_IO_TYPE 0x77

#define ASM_IO_CMD(dir,size,type,sn) (((dir) << 30) | ((size) << 16) | ((type) << 8) | ((sn) << 0))

#define ASHMEM_SET_NAME         ASM_IO_CMD(1, sizeof(char[ASM_NAME_LEN]), ASM_IO_TYPE, 1)
#define ASHMEM_GET_SIZE         ASM_IO_CMD(0, 0, ASM_IO_TYPE, 4)
#define ASHMEM_SET_SIZE         ASM_IO_CMD(1, sizeof(size_t), ASM_IO_TYPE, 3)
#define ASHMEM_SET_PROT_MASK    ASM_IO_CMD(1, sizeof(unsigned long), ASM_IO_TYPE, 5)
#define ASHMEM_PIN              ASM_IO_CMD(1, sizeof(struct asm_pin), ASM_IO_TYPE, 7)
#define ASHMEM_UNPIN            ASM_IO_CMD(1, sizeof(struct asm_pin), ASM_IO_TYPE, 8)

int ashmem_create_block(const char *name, int size);

int ashmem_set_prot_block(int fd, int prot);

int ashmem_pin_block(int fd, struct ashmen_pin *pin);

int ashmem_unpin_block(int fd, struct ashmen_pin *pin);

int ashmem_get_size_block(int fd);

#endif //__MM_ASHMEM_BASE_H__