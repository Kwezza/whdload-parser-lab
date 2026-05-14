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
		-Iinclude -Isrc
	LDFLAGS := -lamiga -lauto
	BUILD_DIR := build/amiga
	BIN := $(BUILD_DIR)/dat_to_tlv
	BIN_FH := $(BUILD_DIR)/filter_harness
	BIN_TC := $(BUILD_DIR)/txcmp
	RUN_CMD = @echo Amiga binary built: $(subst /,\,$(BIN))
else
	CC := gcc
	CFLAGS := -DPLATFORM_HOST=1 -DHOSTBUILD -std=c99 -O2 -Wall -Wextra -Iinclude -Isrc
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
	tools_src/dat_to_tlv_main.c \
	src/whdtlv/core/dat_parser_minimal.c \
	src/whdtlv/core/error_handling.c \
	src/whdtlv/core/field_registry.c \
	src/whdtlv/core/tlv_profile.c \
	src/whdtlv/core/csv_cache.c \
	src/whdtlv/core/filename_processor.c \
	src/whdtlv/core/tlv_builder.c \
	src/whdtlv/core/group_util.c \
	src/whdtlv/core/whdtlv_integration.c \
	src/whdtlv/platform/platform_io.c \
	src/whdtlv/platform/platform_string.c \
	src/whdtlv/io/writeLog.c \
	src/whdtlv/io/pack_types_loader.c \
	src/whdtlv/utils/prettify.c \
	src/whdtlv/utils/crc32.c \
	src/whdtlv/filtering/profile_binder.c \
	src/whdtlv/filtering/selection_plan.c \
	src/whdtlv/filtering/tlv_crc_validate.c \
	src/whdtlv/filtering/tlv_filter.c \
	src/whdtlv/filtering/tlv_group.c \
	src/whdtlv/filtering/tlv_reader.c \
	src/whdtlv/filtering/tlv_results.c \
	src/whdtlv/filtering/tlv_runtime.c \
	src/whdtlv/filtering/tlv_select.c \
	src/whdtlv/filtering/tlv_variant.c \
	src/whdtlv/filtering/whd_search.c \
	src/whdtlv/whdtlv_filter_facade.c

OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

DEMO_OBJ := $(filter-out $(BUILD_DIR)/tools_src/dat_to_tlv_main.o,$(OBJ)) \
            $(BUILD_DIR)/tools/tlv_demo/tlv_demo.o

ifeq ($(TARGET),amiga)
	BIN_DEMO := $(BUILD_DIR)/tlv_demo
else
	BIN_DEMO := $(BUILD_DIR)/tlv_demo.exe
endif

ifeq ($(TARGET),amiga)
	BIN_TEST_FILTER := $(BUILD_DIR)/test_filter_facade
	TEST_FILTER_RUN := @echo Amiga test binary built: $(subst /,\,$(BUILD_DIR))\test_filter_facade -- run on device with STACK 100000
else
	BIN_TEST_FILTER := $(BUILD_DIR)/test_filter_facade.exe
	TEST_FILTER_RUN := $(subst /,\,$(BIN_TEST_FILTER))
	BIN_REPORT := $(BUILD_DIR)/whdtlv_report.exe
	BIN_TEST_REPORT := $(BUILD_DIR)/test_report_csv.exe
endif

# Objects shared by all binaries (everything except the main entry points)
LIB_OBJ := $(filter-out $(BUILD_DIR)/tools_src/dat_to_tlv_main.o,$(OBJ))

TEST_FILTER_OBJ := $(LIB_OBJ) \
                   $(BUILD_DIR)/tests/filtering/test_filter_facade.o

# Reporting subsystem (host-only)
REPORT_SRC := src/whdtlv/reporting/whdtlv_report_csv.c
REPORT_OBJ := $(REPORT_SRC:%.c=$(BUILD_DIR)/%.o)

REPORT_TOOL_OBJ := $(LIB_OBJ) \
                   $(REPORT_OBJ) \
                   $(BUILD_DIR)/tools_src/whdtlv_report/main.o

TEST_REPORT_OBJ := $(LIB_OBJ) \
                   $(REPORT_OBJ) \
                   $(BUILD_DIR)/tests/reporting/test_report_csv.o

ifeq ($(TARGET),amiga)
	BIN_TEST_LANGUAGE :=
else
	BIN_TEST_LANGUAGE := $(BUILD_DIR)/test_language_tokens.exe
endif

TEST_LANGUAGE_OBJ := $(LIB_OBJ) \
                     $(BUILD_DIR)/tests/reporting/test_language_tokens.o

ifeq ($(TARGET),amiga)
	BIN_TEST_EFFECTIVE :=
else
	BIN_TEST_EFFECTIVE := $(BUILD_DIR)/test_effective_columns.exe
endif

TEST_EFFECTIVE_OBJ := $(LIB_OBJ) \
                      $(REPORT_OBJ) \
                      $(BUILD_DIR)/tests/reporting/test_effective_columns.o


help:
	@echo dat_to_tlv standalone build targets
	@echo.
	@echo   make                 - Build host version by default
	@echo   make TARGET=host     - Build host version ^(GCC^) -^> build\host\dat_to_tlv.exe
	@echo   make TARGET=amiga    - Build Amiga version ^(vbcc^) -^> build\amiga\dat_to_tlv
	@echo   make host            - Shortcut for host build
	@echo   make amiga           - Shortcut for Amiga build
	@echo   make run             - Run host binary when TARGET=host
	@echo   make demo            - Build the facade demo program -^> build\host\tlv_demo.exe
	@echo   make test-filter     - Build and run the public filter facade tests
	@echo   make report          - Build the host-side TLV CSV export tool ^(host only^)
	@echo   make test-report     - Build and run the reporting subsystem tests ^(host only^)
	@echo   make test-language   - Build and run the language token validation tests ^(host only^)
	@echo   make test-effective  - Build and run the effective-columns tests ^(host only^)
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

demo: $(BIN_DEMO)

$(BIN_DEMO): $(DEMO_OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(DEMO_OBJ) $(LDFLAGS)

clean:
	@$(call RMDIR_CMD,$(BUILD_DIR))
	@$(call DEL_CMD,output/Games(19-05-2025).tlv)

test-filter: $(BIN_TEST_FILTER)
	$(TEST_FILTER_RUN)

$(BIN_TEST_FILTER): $(TEST_FILTER_OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(TEST_FILTER_OBJ) $(LDFLAGS)

ifneq ($(TARGET),amiga)
report: $(BIN_REPORT)

$(BIN_REPORT): $(REPORT_TOOL_OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(REPORT_TOOL_OBJ) $(LDFLAGS)

test-report: $(BIN_TEST_REPORT)
	$(subst /,\,$(BIN_TEST_REPORT))

$(BIN_TEST_REPORT): $(TEST_REPORT_OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(TEST_REPORT_OBJ) $(LDFLAGS)
test-language: $(BIN_TEST_LANGUAGE)
	$(subst /,\,$(BIN_TEST_LANGUAGE))

$(BIN_TEST_LANGUAGE): $(TEST_LANGUAGE_OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(TEST_LANGUAGE_OBJ) $(LDFLAGS)
test-effective: $(BIN_TEST_EFFECTIVE)
	$(subst /,\,$(BIN_TEST_EFFECTIVE))

$(BIN_TEST_EFFECTIVE): $(TEST_EFFECTIVE_OBJ)
	@$(call MKDIR_CMD,$(BUILD_DIR))
	$(CC) $(CFLAGS) -o $@ $(TEST_EFFECTIVE_OBJ) $(LDFLAGS)
else
report:
	@echo report target is host-only. Use TARGET=host.
test-report:
	@echo test-report target is host-only. Use TARGET=host.
test-language:
	@echo test-language target is host-only. Use TARGET=host.
test-effective:
	@echo test-effective target is host-only. Use TARGET=host.
endif
