# TS3 client on FreeRTOS + FreeRTOS+TCP, QEMU mps2-an385 (Cortex-M3, LAN9118 Ethernet).
# Reuses the official FreeRTOS_Plus_TCP_Echo_Qemu_mps2 demo for startup, UART, DHCP and the NIC driver.
#   make && make run
FREERTOS ?= $(HOME)/ts3deps/FreeRTOS
MBEDTLS  ?= $(HOME)/ts3deps/mbedtls
OPUS     ?= $(HOME)/ts3deps/opus
TS3_HOST ?= 10.0.2.2

DEMO   := $(FREERTOS)/FreeRTOS-Plus/Demo/FreeRTOS_Plus_TCP_Echo_Qemu_mps2
KERNEL := $(FREERTOS)/FreeRTOS/Source
TCP    := $(FREERTOS)/FreeRTOS-Plus/Source/FreeRTOS-Plus-TCP
ROOT   := ../..
B      := build
.DEFAULT_GOAL := $(B)/ts3.axf

CC  = arm-none-eabi-gcc
CXX = arm-none-eabi-g++

INC = -I$(DEMO) -I$(DEMO)/CMSIS -I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_CM3 \
      -I$(TCP)/source/include -I$(TCP)/source/portable/Compiler/GCC \
      -I$(TCP)/source/portable/NetworkInterface/MPS2_AN385/ether_lan9118 \
      -I$(MBEDTLS)/include -I. -I$(ROOT)/third_party/ed25519
DEFS = -DHEAP3 -DMBEDTLS_CONFIG_FILE='"ts3_mbedtls_config.h"' -DTS3_HOST='"$(TS3_HOST)"' -DTS3_BUILD_EPOCH=$(shell date +%s)u
ARCH = -mthumb -mcpu=cortex-m3
CFLAGS   = $(ARCH) -Os -g -ffunction-sections -fdata-sections $(INC) $(DEFS)
CXXFLAGS = $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall
LDFLAGS  = $(ARCH) -T $(DEMO)/mps2_m3.ld -Wl,--gc-sections -Wl,-Map=$(B)/ts3.map \
           -nostartfiles -specs=nano.specs -specs=nosys.specs -specs=rdimon.specs \
           -u _printf_float

DEMO_SRC = $(DEMO)/startup.c $(DEMO)/syscalls.c $(DEMO)/main.c $(DEMO)/main_networking.c \
           $(KERNEL)/tasks.c $(KERNEL)/list.c $(KERNEL)/queue.c $(KERNEL)/event_groups.c \
           $(KERNEL)/portable/GCC/ARM_CM3/port.c $(KERNEL)/portable/MemMang/heap_3.c \
           $(wildcard $(TCP)/source/*.c) \
           $(TCP)/source/portable/BufferManagement/BufferAllocation_2.c \
           $(TCP)/source/portable/NetworkInterface/MPS2_AN385/NetworkInterface.c \
           $(TCP)/source/portable/NetworkInterface/MPS2_AN385/ether_lan9118/smsc9220_eth_drv.c
LIB_SRC  = $(wildcard $(MBEDTLS)/library/*.c) \
           $(ROOT)/third_party/ed25519/fe.c $(ROOT)/third_party/ed25519/ge.c $(ROOT)/third_party/ed25519/sc.c
CXX_SRC  = app.cpp port_freertos.cpp $(ROOT)/src/ts3.cpp $(ROOT)/src/crypto.cpp $(ROOT)/test/selftest.cpp
ifdef CONF_PORT
CXX_SRC += $(ROOT)/test/port_conformance.cpp
DEFS    += -DTS3_CONFORMANCE_PORT=$(CONF_PORT)
endif

# libopus, fixed-point (no FPU on Cortex-M3), source lists straight from opus' own .mk files
include $(OPUS)/celt_sources.mk $(OPUS)/silk_sources.mk $(OPUS)/opus_sources.mk
OPUS_SRC = $(addprefix $(OPUS)/,$(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FIXED) $(OPUS_SOURCES))
LIB_SRC += $(OPUS_SRC)
INC += -I$(OPUS)/include

obj = $(B)/$(subst /,_,$(subst $(HOME)/,,$(abspath $(1)))).o
OBJS = $(foreach f,$(DEMO_SRC) $(LIB_SRC) $(CXX_SRC),$(call obj,$(f)))
$(foreach f,$(OPUS_SRC),$(call obj,$(f))): CFLAGS += -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API \
    -DVAR_ARRAYS -I$(OPUS)/celt -I$(OPUS)/silk -I$(OPUS)/silk/fixed
$(call obj,app.cpp): clip.raw

$(B)/ts3.axf: $(OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@
	arm-none-eabi-size $@

define rule
$(call obj,$(1)): $(1) | $(B)
	$(if $(filter %.cpp,$(1)),$$(CXX) $$(CXXFLAGS) $(if $(filter %selftest.cpp,$(1)),-Dmain=ts3_selftest_main),$$(CC) $$(CFLAGS)) -c $$< -o $$@
endef
$(foreach f,$(DEMO_SRC) $(LIB_SRC) $(CXX_SRC),$(eval $(call rule,$(f))))

$(B):
	mkdir -p $@

run: $(B)/ts3.axf
	qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel $< -monitor null \
	  -semihosting -semihosting-config enable=on,target=native -serial stdio -nographic \
	  -netdev user,id=n0 -net nic,model=lan9118,netdev=n0

clean:
	rm -rf $(B)

.PHONY: run clean
