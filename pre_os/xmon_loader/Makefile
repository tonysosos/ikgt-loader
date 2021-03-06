################################################################################
# Copyright (c) 2015 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
ifndef PROJS
export PROJS = $(CURDIR)/../../..

export CC = gcc
export AS = gcc
export LD = ld

debug ?= 0
ifeq ($(debug), 1)
LOADER_CMPL_OPT_FLAGS = -DDEBUG
export BINDIR = $(PROJS)/bin/linux/debug/
export OUTDIR = $(PROJS)/loader/pre_os/build/linux/debug/
else
export BINDIR = $(PROJS)/bin/linux/release/
export OUTDIR = $(PROJS)/loader/pre_os/build/linux/release/
endif


$(shell mkdir -p $(OUTDIR))
$(shell mkdir -p $(BINDIR))
export LOADER_CMPL_OPT_FLAGS
endif

CSOURCES = $(wildcard *.c)
ASOURCES = $(wildcard *.S)
include $(PROJS)/loader/rule.linux

INCLUDES += -I$(PROJS)/loader/pre_os/common/include \
            -Iutils/memory \
            -Iutils/screen \
            -Iutils/x64

OBJS = $(OUTDIR)xmon_loader.o \
       $(OUTDIR)e820.o \
       $(OUTDIR)idt.o \
       $(OUTDIR)screen.o \
       $(OUTDIR)memory.o \
       $(OUTDIR)x32_gdt64.o \
       $(OUTDIR)x32_pt64.o \
       $(OUTDIR)elf_info.o \
       $(OUTDIR)elf32_ld.o \
       $(OUTDIR)elf64_ld.o \
       $(OUTDIR)elf_ld.o \
       $(OUTDIR)ia32_low_level.o \
       $(OUTDIR)image_access_mem.o \
       $(OUTDIR)common.o \
       $(OUTDIR)pg_entry.o \
       $(OUTDIR)primary_guest.o \
       $(OUTDIR)linux_loader.o

TARGET = xmon_loader.elf

ifeq ($(debug), 1)
LDFLAGS = -e xmon_loader -m elf_i386 -pie -static
else
LDFLAGS = -e xmon_loader -m elf_i386 -pie -s -static
endif

.PHONY: all ia32 ld util common $(TARGET) clean

all: ia32 ld util common $(TARGET)

ia32:
	$(MAKE) -C $(PROJS)/core/common/libc/ia32

ld:
	$(MAKE) -C $(PROJS)/loader/common/ld
	
util:
	$(MAKE) -C $(PROJS)/loader/pre_os/xmon_loader/utils

common:
	$(MAKE) -C $(PROJS)/loader/common/util

$(TARGET):
	$(LD) $(LDFLAGS) -o $(OUTDIR)$@ $(OBJS)
	
clean:
	rm -f $(OBJS)
	rm -f $(OUTDIR)xmon_loader.*
	
