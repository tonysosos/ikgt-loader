#!/bin/bash

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

# To run this script under Cygwin: Open Windows Command Prompt;
# execute: bash build_loader.sh.

###############################################################################
# hard-coded xmon memory size to 6MB for now
# later on it would be changed to use dynamically calculated value
###############################################################################
xmon_mem_size=6

###############################################################################
# load_base is used for GRUB only. For SFI, load base is obtained from E820 at 
# run time, not hard-coded.
# 6MB memory region: 0x10000000 to 0x10600000
# ikgt_pkg.bin will be loaded to 0x10000000.
# During boot process, ikgt_pkg.bin would be running within the lowest 1MB memory.
# xmon and startap would be loaded to 0x10100000.
###############################################################################
load_base=0x10000000

if [ "$1" == "debug" ]; then
    debug_port_io_addr=0x3f8
    debug_port_control=0x01000001
else
    debug_port_io_addr=0x3f8
    debug_port_control=0x01000000
fi
################################################################################
# Note 1: Definition of debug_port_control:
#
#         [7:0]   - 0: disable use of debug port, 1: enable use of debug port
#         [15:8]  - Not used
#         [23:16] - Not used
#         [31:24] - 0: use 0x3F8, 1: use debug_port_io_addr
# 
################################################################################

#############################################################################
# Find all files
#############################################################################

files=" \
    "build/linux/$1/chain_load.bin" \
    "build/linux/$1/starter.bin" \
    "build/linux/$1/xmon_loader.elf" \
    "../../bin/linux/$1/startap.elf" \
    "../../bin/linux/$1/$2" \
"

for x in $files; do
    if [ ! -f $x ]; then
        echo "  Can't find $x."
        exit
    fi
    cp $x .
done

#############################################################################
# Convert text to hex
#############################################################################

function Dump()
{
  for t in $@; do
    x=$((t))
    x3=$((($x >> 24) & 0xff))
    x2=$((($x >> 16) & 0xff))
    x1=$((($x >>  8) & 0xff))
    x0=$((($x >>  0) & 0xff))
    y=$(printf "\\\x%02x\\\x%02x\\\x%02x\\\x%02x" $x0 $x1 $x2 $x3)
    printf $y
  done
}

#############################################################################
# XmonDesc struct, each element is a uint32
#############################################################################

# File sizes in 512-byte blocks

s=$(stat -c%s "starter.bin")
StarterStart=2
StarterCount=$(((s + 511) / 512))

s=$(stat -c%s "xmon_loader.elf")
XmonLoaderStart=$((StarterStart + StarterCount))
XmonLoaderCount=$(((s + 511) / 512))

s=$(stat -c%s "startap.elf")
StartApStart=$((XmonLoaderStart + XmonLoaderCount))
StartApCount=$(((s + 511) / 512))

s=$(stat -c%s "$2")
XmonStart=$((StartApStart + StartApCount))
XmonCount=$(((s + 511) / 512))

StartDescStart=$((XmonStart + XmonCount))
StartDescCount=1

Guest0DescStart=$((StartDescStart + StartDescCount))
Guest0DescCount=1

# The Multiboot hader: offsets must match mem_map.h
# This is used for GRUB only
MbMagic=0x1badb002
MbFlag=0x00010003
MbCksum=$((0 - MbMagic - MbFlag))
MbHdrAddr=$((load_base + 0x00000050))
MbText=$((load_base + 0x0000000))
# report all the memory size used by xmon to grub
MbBss=$((load_base + 0x100000 * xmon_mem_size))
MbEnd=$MbBss
MbEntry=$((load_base + 0x00000400))

XmonDesc=" \
    XmonDescSize=80 \
    XmonDescVer=0x00002000 \
    XmonDescSectors=1 \
    UmbrSize=0 \
    XmonMemMb=$xmon_mem_size \
    GuestCount=0 \
    XmonlStart=0 \
    XmonlCount=0 \
    StarterStart=$StarterStart \
    StarterCount=$StarterCount \
    XmonLoaderStart=$XmonLoaderStart \
    XmonLoaderCount=$XmonLoaderCount \
    StartApStart=$StartApStart \
    StartApCount=$StartApCount \
    XmonStart=$XmonStart \
    XmonCount=$XmonCount \
    StartDescStart=$StartDescStart \
    StartDescCount=$StartDescCount \
    Guest0DescStart=$Guest0DescStart \
    Guest0DescCount=$Guest0DescCount \
    MbMagic=$MbMagic \
    MbFlag=$MbFlag \
    MbCksum=$MbCksum \
    MbHdrAddr=$MbHdrAddr \
    MbText=$MbText \
    MbBss=$MbBss \
    MbEnd=$MbEnd \
    MbEntry=$MbEntry \
"

XmonDesc=`echo $XmonDesc | sed 's/[A-Za-z0-9]*=//g'`

#############################################################################
# StartDesc struct, each element is a uint32
#############################################################################

StartDesc=" \
    StartDescSizeVer=(0x00e8,0x0005) \
    StartDescInstCpuBootCpu=(0x0001,0x0000) \
    StartDescMonGstsMonStkPgs=(0x0000,0x000a) \
    StartDescVendor=0 \
    StartDescFlags=0 \
    StartDescDeviceOwner=0 \
    StartDescAcpiOwner=0 \
    StartDescNmiOwner=0 \
    StartDescMem00TotalSize=0 \
    StartDescMem00ImageSize=0 \
    StartDescMem00BaseAddrLow=0 \
    StartDescMem00BaseAddrHigh=0 \
    StartDescMem00EntryPointLow=0 \
    StartDescMem00EntryPointHigh=0 \
    StartDescMem01TotalSize=0 \
    StartDescMem01ImageSize=0 \
    StartDescMem01BaseAddrLow=0 \
    StartDescMem01BaseAddrHigh=0 \
    StartDescMem01EntryPointLow=0 \
    StartDescMem01EntryPointHigh=0 \
    StartDescE820LayoutLow=0 \
    StartDescE820LayoutHigh=0 \
    StartDescGuestState00Low=0 \
    StartDescGuestState00High=0 \
    StartDescGuestState01Low=0 \
    StartDescGuestState01High=0 \
    DebugVerbosity=0x00000004 \
    DebugReserved=0 \
    DebugPort00Type=$debug_port_control \
    DebugPort00Base=$debug_port_io_addr \
    DebugPort01Type=0 \
    DebugPort01Base=0x00000000 \
    DebugMaskLow=0xffffffff \
    DebugMaskHigh=0xffffffff \
    DebugBufferLow=0 \
    DebugBufferHigh=0 \
    ApicId00=0xffffffff \
    ApicId01=0xffffffff \
    ApicId02=0xffffffff \
    ApicId03=0xffffffff \
    ApicId04=0xffffffff \
    ApicId05=0xffffffff \
    ApicId06=0xffffffff \
    ApicId07=0xffffffff \
    ApicId08=0xffffffff \
    ApicId09=0xffffffff \
    ApicId10=0xffffffff \
    ApicId11=0xffffffff \
    ApicId12=0xffffffff \
    ApicId13=0xffffffff \
    ApicId14=0xffffffff \
    ApicId15=0xffffffff \
    ApicId16=0xffffffff \
    ApicId17=0xffffffff \
    ApicId18=0xffffffff \
    ApicId19=0xffffffff \
"

StartDesc=`echo $StartDesc | sed 's/(0x\(....\),0x\(....\))/0x\2\1/g'`
StartDesc=`echo $StartDesc | sed 's/[A-Za-z0-9]*=//g'`

#############################################################################
# GuestDesc struct, each emelemnt is a uint32
#############################################################################

GuestDesc=" \
    GuestDescSizeVer=(0x003c,0x0001) \
    GuestDescFlags=3 \
    GuestDescMagic=0 \
    GuestDescCpuAffinity=0xffffffff \
    GuestDescCpuStateCount=0 \
    GuestDescDeviceCount=0 \
    GuestDescImageSize=0 \
    GuestDescImageAddrHigh=0 \
    GuestDescImageAddrLow=0 \
    GuestDescPhyMemSize=0 \
    GuestDescImageOffsetGuestPhysical=0 \
    GuestDescCpuStateAddrHigh=0 \
    GuestDescCpuStateAddrLow=0 \
    GuestDescDeviceArrayAddrHigh=0 \
    GuestDescDeviceArrayAddrLow=0 \
"
GuestDesc=`echo $GuestDesc | sed 's/(0x\(....\),0x\(....\))/0x\2\1/g'`
GuestDesc=`echo $GuestDesc | sed 's/[A-Za-z0-9]*=//g'`

#############################################################################
# Build the loader binary
#############################################################################

Dump $XmonDesc | dd of=ikgt_pkg.bin seek=0
dd if=chain_load.bin of=ikgt_pkg.bin seek=1
dd if=starter.bin of=ikgt_pkg.bin seek=$StarterStart
dd if=xmon_loader.elf of=ikgt_pkg.bin seek=$XmonLoaderStart
dd if=startap.elf of=ikgt_pkg.bin seek=$StartApStart
dd if=$2 of=ikgt_pkg.bin seek=$XmonStart
Dump $StartDesc | dd of=ikgt_pkg.bin seek=$StartDescStart
Dump $GuestDesc | dd of=ikgt_pkg.bin seek=$Guest0DescStart

cp ./ikgt_pkg.bin ../../bin/linux/$1/ikgt_pkg.bin
#rm -f *.elf *.bin


# End of file
