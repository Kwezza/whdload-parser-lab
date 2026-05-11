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
	BIN_FH := $(BUILD_DIR)/filter_harness
	BIN_TC := $(BUILD_DIR)/txcmp
	RUN_CMD = @echo Amiga binary built: $(subst /,\,$(BIN))
else
	CC := gcc
	CFLAGS := -DPLATFORM_HOST=1 -DHOSTBUILD -std=c99 -O2 -Wall -Wextra -Iinclude -Iinclude/platform -Iinclude_raw -Iapp_src
	LDFLAGS :=
	BUILD_DIR := build/host
	BIN := $(BUILD_DIR)/dat_to_tlv.exe
	BIN_FH := $(BUILD_DIR)/filter_harness.exe
	BIN_TC := $(BUILD_DIR)/txcmp.exe
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
	src_raw/group_util.c \
	src/platform/platform_io.c \
	src/platform/platform_string.c \
	src/io/writeLog.c \
	src/io/pack_types_loader.c \
	src/utils/prettify.c \
	src/utils/crc32.c

OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

# Filter harness sources (Stage A: scaffold only; expanded in later chunks)
SRC_FH := \
	tools/filter_harness/main.c \
	src_raw/filtering/tlv_filter.c \
	src_raw/filtering/tlv_runtime.c \
	src_raw/filtering/tlv_reader.c \
	src_raw/filtering/tlv_crc_validate.c \
	src_raw/filtering/tlv_variant.c \
	src_raw/filtering/tlv_group.c \
	src_raw/filtering/tlv_select.c \
	src_raw/filtering/tlv_results.c \
	src_raw/filtering/profile_binder.c \
	src_raw/filtering/selection_plan.c \
	src_raw/filtering/whd_search.c \
	src_raw/group_util.c \
	src/platform/platform_io.c \
	src/platform/platform_string.c \
	src/utils/crc32.c

OBJ_FH := $(SRC_FH:%.c=$(BUILD_DIR)/%.o)

# Fixture generator sources (Stage J: regression fixtures; host-only)
SRC_GF := tools/gen_fixture_tlv/gen_fixture_tlv.c
BIN_GF  := $(BUILD_DIR)/gen_fixture_tlv.exe

# Text comparison utility (host and Amiga)
SRC_TC := tools/txcmp/txcmp.c
OBJ_TC := $(SRC_TC:%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean run help host amiga filter_harness gen_fixture_tlv gen_fixtures test_filter txcmp amiga_test_bins

all: $(BIN)

host:
	$(MAKE) TARGET=host all

amiga:
	$(MAKE) TARGET=amiga all

filter_harness: $(BIN_FH)

$(BIN_FH): $(OBJ_FH)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(OBJ_FH) $(LDFLAGS)

txcmp: $(BIN_TC)

$(BIN_TC): $(OBJ_TC)
	@$(call MKDIR_CMD,$(BUILD_DIR)/tools/txcmp)
	$(CC) $(CFLAGS) -o $@ $(OBJ_TC) $(LDFLAGS)

amiga_test_bins:
	$(MAKE) TARGET=amiga filter_harness
	$(MAKE) TARGET=amiga txcmp

help:
	@echo dat_to_tlv standalone build targets
	@echo.
	@echo   make                 - Build host version by default
	@echo   make TARGET=host     - Build host version ^(GCC^) -^> build\host\dat_to_tlv.exe
	@echo   make TARGET=amiga    - Build Amiga version ^(vbcc^) -^> build\amiga\dat_to_tlv
	@echo   make host            - Shortcut for host build
	@echo   make amiga           - Shortcut for Amiga build
	@echo   make run             - Run host binary when TARGET=host
	@echo   make filter_harness  - Build filter_harness binary
	@echo   make txcmp           - Build txcmp text comparison utility
	@echo   make amiga_test_bins - Build filter_harness + txcmp for Amiga ^(TARGET=amiga^)
	@echo   make gen_fixture_tlv - Build the fixture TLV generator
	@echo   make gen_fixtures    - Build generator and emit TLV files to tests\filtering\tlv\
	@echo   make test_filter     - Run regression tests against fixture TLVs
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

$(BIN_GF): $(SRC_GF)
	@$(call MKDIR_CMD,$(BUILD_DIR)/tools/gen_fixture_tlv)
	$(CC) $(CFLAGS) -o $@ $(SRC_GF)

gen_fixture_tlv: $(BIN_GF)

gen_fixtures: $(BIN_GF)
	@$(call MKDIR_CMD,tests\filtering\tlv)
	$(subst /,\,$(BIN_GF)) --out-dir tests\filtering\tlv

test_filter: $(BIN_FH) gen_fixtures
	tests\filtering\run_tests.bat $(subst /,\,$(BIN_FH))

clean:
	@$(call RMDIR_CMD,$(BUILD_DIR))
	@$(call DEL_CMD,output/Games(19-05-2025).tlv)
