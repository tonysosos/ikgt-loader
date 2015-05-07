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

CSOURCES = starter_main.c \
           run_xmon_loader.c

ASOURCES = starter.S

include $(PROJS)/loader/rule.linux

INCLUDES += -I$(PROJS)/loader/pre_os/xmon_loader/utils/screen 

TARGET = starter.elf

ifeq ($(debug), 1)
LDFLAGS = -e start -m elf_i386 -pie -static
else
LDFLAGS = -e start -m elf_i386 -pie -s -static
endif

OBJS = $(OUTDIR)starter.o $(OUTDIR)starter_main.o \
       $(OUTDIR)run_xmon_loader.o $(OUTDIR)elf_ld.o \
       $(OUTDIR)elf32_ld.o $(OUTDIR)elf64_ld.o \
       $(OUTDIR)elf_info.o $(OUTDIR)image_access_mem.o \
       $(OUTDIR)memory.o $(OUTDIR)screen.o \
       $(OUTDIR)common.o
	   
.PHONY: all ld utils common $(TARGET) copy clean chain_load.bin

all: ld utils common $(TARGET) copy chain_load.bin

chain_load.bin:
	as chain_load.S -o $(OUTDIR)chain_load.o
	$(LD) --oformat binary $(OUTDIR)chain_load.o -o $(OUTDIR)chain_load.bin
	
ld:
	$(MAKE) -C $(PROJS)/loader/common/ld
	
utils:
	$(MAKE) -C $(PROJS)/loader/pre_os/xmon_loader/utils	

common:
	$(MAKE) -C $(PROJS)/loader/common/util	
	
$(TARGET):
	$(LD) $(LDFLAGS) -o $(OUTDIR)$@ $(OBJS)	

copy:
	objcopy -j .text -O binary -S $(OUTDIR)starter.elf $(OUTDIR)starter.bin

clean:
	rm -f $(OBJS) $(OUTDIR)pe_ld.o
	rm -f $(OUTDIR)starter.* $(OUTDIR)chain_load.*
