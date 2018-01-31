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

#include "multimedia/ashmem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

int ashmem_create_block(const char *name, int size)
{
    int fd = -1;
    char buf[ASM_NAME_LEN] = {'\0'};

	fd = open(ASM_NAME, O_RDWR);
    if (fd < 0) {
        printf("open ashmem failed");
        return fd;
    }

    if (name != NULL) {
		strncpy(buf, name, ASM_NAME_LEN - 1);
		if(ioctl(fd, ASHMEM_SET_NAME, buf) < 0) {
            close(fd);
            return -1;
        }
    }

	if (ioctl(fd, ASHMEM_SET_SIZE, size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int ashmem_set_prot_block(int fd, int prot)
{
	return ioctl(fd, ASHMEM_SET_PROT_MASK, prot);
}

int ashmem_pin_block(int fd, struct ashmen_pin *pin)
{
    if (pin == NULL) {
        return -1;
    }

	return ioctl(fd, ASHMEM_PIN, pin);
}

int ashmem_unpin_block(int fd, struct ashmen_pin *pin)
{
    if (pin == NULL) {
        return -1;
    }

	return ioctl(fd, ASHMEM_UNPIN, pin);
}

int ashmem_get_size_block(int fd)
{
  return ioctl(fd, ASHMEM_GET_SIZE, NULL);
}
