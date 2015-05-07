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

.file   "chain_load.s"

#generate 16-bit code
.code16
.text

.globl	_start
.org 0000
_start: 

    # dx indicates the boot sector.

    testw %dx, %dx
    jns read_mbr
    int $0x19
    jmp .

read_mbr: 

    movzbw %dh, %bp
    movw $0x201, %ax
    movw $0x001, %cx
    movw $0x7c00, %bx
    movb $0x000, %dh
    int $0x13
    jc disk_error

    # No logical partition support.

    testw %bp, %bp
    jz start_guest
    cmpw $4, %bp
    jg disk_error

read_part: 

    subl $16, %esp
    xorl %esi, %esi
    movl %esp, %esi
    movw $0x010, 0(%esi)            # dap->size
    movw $0x001, 2(%esi)            # dap->sector_count
    movl $0x7c00, 4(%esi)           # dap->buffer
    movl $0x000, 12(%esi)           # dap->start_lba_high

    xorl %edi, %edi
    movw %bp, %di
    decw %di
    shlw $4, %di

    # partition table offset is 0x01be.

    movl 0x7c00 + 0x1be(%edi), %edi
    movl %edi, 8(%esi) # dap->start_lba_low
    movb $0x42, %ah
    int $0x13
    jc disk_error

start_guest: 
  	jmp buffer
  	
disk_error: 
    jmp .

.org 0x5c00
buffer:

.end
