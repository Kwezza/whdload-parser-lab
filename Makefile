SHELL := cmd
.SHELLFLAGS := /C

VBCC ?= C:/VBCC
AMIGA_INCLUDES := $(VBCC)/targets/m68k-amigaos/include

TARGET ?= host
PROFILE ?= 0

ifeq ($(TARGET),amiga)
	CC := $(VBCC)/bin/vc
	CFLAGS := -DPLATFORM_AMIGA +aos68k -cpu=68000 -O2 \
		-I$(AMIGA_INCLUDES) \
		-Iinclude -Iinclude/platform -Iinclude_raw -Iapp_src
	LDFLAGS := -lamiga -lauto
	BUILD_DIR := build/amiga
	BIN := $(BUILD_DIR)/dat_to_tlv
	RUN_CMD = @echo Amiga binary built: $(subst /,\,$(BIN))
else
	CC := gcc
	CFLAGS := -DPLATFORM_HOST=1 -DHOSTBUILD -std=c99 -O2 -Wall -Wextra -Iinclude -Iinclude/platform -Iinclude_raw -Iapp_src
	LDFLAGS :=
	BUILD_DIR := build/host
	BIN := $(BUILD_DIR)/dat_to_tlv.exe
	RUN_CMD = $(subst /,\,$(BIN))
endif

ifeq ($(PROFILE),1)
	CFLAGS += -DTLV_PROFILE_ENABLE=1
endif

MKDIR_CMD = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
RMDIR_CMD = if exist "$(subst /,\,$(1))" rmdir /s /q "$(subst /,\,$(1))"
DEL_CMD = if exist "$(subst /,\,$(1))" del /q "$(subst /,\,$(1))"

SRC := \
	app_src/main.c \
	app_src/dat_parser_minimal.c \
	src_raw/error_handling.c \
	src_raw/field_registry.c \
	src_raw/tlv_profile.c \
	src_raw/csv_cache.c \
	src_raw/filename_processor.c \
	src_raw/tlv_builder.c \
	src/platform/platform_io.c \
	src/platform/platform_string.c \
	src/io/writeLog.c \
	src/io/pack_types_loader.c \
	src/utils/prettify.c \
	src/utils/crc32.c

OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean run help host amiga

all: $(BIN)

host:
	$(MAKE) TARGET=host all

amiga:
	$(MAKE) TARGET=amiga all

help:
	@echo dat_to_tlv standalone build targets
	@echo.
	@echo   make                 - Build host version by default
	@echo   make TARGET=host     - Build host version ^(GCC^) -^> build\host\dat_to_tlv.exe
	@echo   make TARGET=amiga    - Build Amiga version ^(vbcc^) -^> build\amiga\dat_to_tlv
	@echo   make host            - Shortcut for host build
	@echo   make amiga           - Shortcut for Amiga build
	@echo   make run             - Run host binary when TARGET=host
	@echo   make clean           - Remove build output for current TARGET and default TLV output
	@echo.
	@echo Variables:
	@echo   TARGET=host^|amiga   - Select build target
	@echo   PROFILE=0^|1         - Enable TLV pipeline profiling summary when set to 1
	@echo   VBCC=C:/VBCC         - Root of vbcc installation for Amiga builds

$(BIN): $(OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@$(call MKDIR_CMD,$(dir $@))
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	$(RUN_CMD)

clean:
	@$(call RMDIR_CMD,$(BUILD_DIR))
	@$(call DEL_CMD,output/Games(19-05-2025).tlv)
