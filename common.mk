################################################################################
# \file common.mk
# \version 1.0
#
# \brief
# Settings shared across all projects.
#
################################################################################
# \copyright
# (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG.  SPDX-License-Identifier: Apache-2.0
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

MTB_TYPE=PROJECT

# Target board/hardware (BSP).
# To change the target, it is recommended to use the Library manager
# ('make modlibs' from command line), which will also update Eclipse IDE launch
# configurations. If TARGET is manually edited, ensure TARGET_<BSP>.mtb with a
# valid URL exists in the application, run 'make getlibs' to fetch BSP contents
# and update or regenerate launch configurations for your IDE.
TARGET=KIT_PSC3M8_EVK

# Name of toolchain to use. Options include:
#
# GCC_ARM -- GCC provided with ModusToolbox IDE
# ARM     -- ARM Compiler (must be installed separately)
# IAR     -- IAR Compiler (must be installed separately)
#
# See also: CY_COMPILER_PATH below
TOOLCHAIN=GCC_ARM

# Default build configuration. Options include:
#
# Debug -- build with minimal optimizations, focus on debugging.
# Release -- build with full optimizations
# Custom -- build with custom configuration, set the optimization flag in CFLAGS
# 
# If CONFIG is manually edited, ensure to update or regenerate launch configurations 
# for your IDE.
CONFIG=Debug

# Set Python Path
PYTHON=python

# Path to Image signing private key
IMAGE_SIGNING_KEY=

# Set Image type as BOOT or UPDATE
#  * BOOT   : Generated Image will have Primary Slot (Suitable for directly Programming the image through KitProg3)
#  * UPDATE : Generated Image will use Shared Secondary Slot (SSS) (To be used for DFU Host Tool, Programming the image through MiniProg4 )
IMG_TYPE=BOOT

# Set Image Version and Build Number
ifeq ($(IMG_TYPE),BOOT)
IMG_VER_MAJOR=1		# 0 - 255
IMG_VER_MINOR=0		# 0 - 255
IMG_REVISION=0		# 0 - 65535
IMG_BUILD_NO=0		# 0 - 65535
else
IMG_VER_MAJOR=2		# 0 - 255
IMG_VER_MINOR=0		# 0 - 255
IMG_REVISION=0		# 0 - 65535
IMG_BUILD_NO=1		# 0 - 65535
endif #$(IMG_TYPE)

# MCU boot header size, reserved at the beginning of the image
MCUBOOT_HEADER_SIZE=0x400

# Main Core and PPCA Core LED toggle interval setting
ifeq ($(IMG_TYPE),BOOT)
LED_TOGGLE_INTERVAL_MS=1000
else
LED_TOGGLE_INTERVAL_MS=500
endif #$(IMG_TYPE)

#Config file for postbuild sign operations.
#NOTE:Check the JSON file for the command parameters
ifeq ($(IMG_TYPE), BOOT)
COMBINE_SIGN_JSON?=configs/boot.json
else
COMBINE_SIGN_JSON?=configs/update.json
endif #$(IMG_TYPE)

include ../common_app.mk
