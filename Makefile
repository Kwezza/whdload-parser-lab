CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic
CPPFLAGS ?= -Isrc/core -Isrc/harness
LDFLAGS ?=

TARGET := variant_harness
SRCS := src/harness/main.c \
	src/core/vh_csv.c \
	src/core/vh_fields.c \
	src/core/vh_memtrack.c \
	src/core/vh_parse.c \
	src/core/vh_profile.c \
	src/core/vh_score.c \
	src/core/vh_group.c \
	src/core/vh_string_pool.c
OBJS := $(SRCS:.c=.o)
STRING_POOL_TEST := tests/vh_string_pool_test
PARSE_GROUP_KEY_TEST := tests/vh_parse_group_key_test
TEST_BINS := $(STRING_POOL_TEST) $(PARSE_GROUP_KEY_TEST)

AMIGA_CC ?= vc
AMIGA_AUTO ?= 1

AMIGA_BUILD_DIR := ../../build/amiga/varianttest
AMIGA_BIN := ../../Bin/Amiga/varianttest

AMIGA_SRCS := src/harness/amiga_varianttest.c \
	src/harness/vh_amiga_builtin_compat.c \
	src/core/vh_csv.c \
	src/core/vh_memtrack.c \
	src/core/vh_parse.c \
	src/core/vh_profile.c \
	src/core/vh_score.c \
	src/core/vh_group.c \
	src/core/vh_string_pool.c
AMIGA_OBJS := $(addprefix $(AMIGA_BUILD_DIR)/,$(notdir $(AMIGA_SRCS:.c=.o)))

ROADSHOW_INC ?= C:/Amiga/Roadshow-SDK-1.8/netinclude
NDK_INC ?= C:/Amiga/AmigaIncludes

AMIGA_CFLAGS := +aos68k -c99 -cpu=68000 -O2 -size \
	-I$(VBCC)/targets/m68k-amigaos/include \
	-Isrc/core \
	-Isrc/harness \
	-I$(NDK_INC) \
	-I$(ROADSHOW_INC) \
	-DPLATFORM_AMIGA=1 \
	-D__AMIGA__ \
	-DDEBUG \
	-DVH_AMIGA_MINIMAL=1

AMIGA_LDFLAGS_BASE := +aos68k -cpu=68000 -O2 -size -final -lamiga
ifeq ($(AMIGA_AUTO),1)
AMIGA_LDFLAGS := $(AMIGA_LDFLAGS_BASE) -lauto
else
AMIGA_LDFLAGS := $(AMIGA_LDFLAGS_BASE)
endif

ifeq ($(OS),Windows_NT)
AMIGA_MKDIR_BUILD = cmd /C "if not exist ..\\..\\build\\amiga\\varianttest mkdir ..\\..\\build\\amiga\\varianttest"
AMIGA_MKDIR_BIN = cmd /C "if not exist ..\\..\\Bin\\Amiga mkdir ..\\..\\Bin\\Amiga"
else
AMIGA_MKDIR_BUILD = mkdir -p ../../build/amiga/varianttest
AMIGA_MKDIR_BIN = mkdir -p ../../Bin/Amiga
endif

.PHONY: all clean help test test-build varianttest-amiga check-core-no-stdio-print

help:
	@echo whdload-parser-lab build targets
	@echo.
	@echo   make all               Build host parser executable (variant_harness)
	@echo   make test-build        Build host parser + test executables only
	@echo   make test              Build and run all tests
	@echo   make varianttest-amiga Build Amiga executable (requires vbcc/vc setup)
	@echo   make clean             Remove host/amiga build artifacts
	@echo.
	@echo Common variables:
	@echo   CC=compiler            Host C compiler override (default: gcc)
	@echo   CFLAGS=flags           Host compiler flags
	@echo   LDFLAGS=flags          Host linker flags
	@echo   AMIGA_CC=compiler      Amiga compiler override (default: vc)
	@echo   AMIGA_AUTO=0 or 1      Toggle -lauto link flag (default: 1)
	@echo.
	@echo Examples:
	@echo   make test-build
	@echo   make test
	@echo   make varianttest-amiga AMIGA_AUTO=0

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check-core-no-stdio-print:
ifeq ($(OS),Windows_NT)
	@powershell -NoProfile -Command "$$matches = Select-String -Path 'src/core/*.c','src/core/*.h' -Pattern '(^|[^A-Za-z0-9_])(printf|fprintf|snprintf)\s*\('; if ($$matches) { Write-Host 'Error: stdio print formatting is forbidden in src/core'; $$matches | ForEach-Object { Write-Host ($$_.Path + ':' + $$_.LineNumber + ': ' + $$_.Line.Trim()) }; exit 1 }"
else
	@if grep -R -n -E '(^|[^A-Za-z0-9_])(printf|fprintf|snprintf)[[:space:]]*\(' src/core > /dev/null; then \
		echo "Error: stdio print formatting is forbidden in src/core"; \
		grep -R -n -E '(^|[^A-Za-z0-9_])(printf|fprintf|snprintf)[[:space:]]*\(' src/core; \
		exit 1; \
	fi
endif

test: $(TARGET) check-core-no-stdio-print
	$(MAKE) test-build
ifeq ($(OS),Windows_NT)
	cmd /C tests\\run_tests_windows.bat
else
	./$(STRING_POOL_TEST)
	./$(PARSE_GROUP_KEY_TEST)
	./$(TARGET) --select tests/filenames_basic.txt --profile default > test_output/basic_select.txt
	./$(TARGET) --report tests/filenames_basic.txt --profile default > test_output/basic_report.txt
	./$(TARGET) --report tests/filenames_memory.txt --profile default > test_output/memory_report.txt
	./$(TARGET) --report tests/filenames_language.txt --profile default > test_output/language_report.txt
	./$(TARGET) --report tests/filenames_special_report_only.txt --profile default > test_output/special_report_only.txt
endif

test-build: $(TARGET) $(TEST_BINS)

$(STRING_POOL_TEST): tests/test_vh_string_pool.c src/core/vh_string_pool.c src/core/vh_memtrack.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_vh_string_pool.c src/core/vh_string_pool.c src/core/vh_memtrack.c -o $(STRING_POOL_TEST)

$(PARSE_GROUP_KEY_TEST): tests/test_vh_parse_group_key.c src/core/vh_parse.c src/core/vh_csv.c src/core/vh_string_pool.c src/core/vh_memtrack.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_vh_parse_group_key.c src/core/vh_parse.c src/core/vh_csv.c src/core/vh_string_pool.c src/core/vh_memtrack.c -o $(PARSE_GROUP_KEY_TEST)

varianttest-amiga: $(AMIGA_BIN)

$(AMIGA_BIN): $(AMIGA_OBJS)
	$(AMIGA_CC) $(AMIGA_LDFLAGS) -o $@ $^

$(AMIGA_BUILD_DIR):
	$(AMIGA_MKDIR_BUILD)

../../Bin/Amiga:
	$(AMIGA_MKDIR_BIN)

$(AMIGA_BIN): | ../../Bin/Amiga

$(AMIGA_BUILD_DIR)/%.o: src/core/%.c | $(AMIGA_BUILD_DIR)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -c $< -o $@

$(AMIGA_BUILD_DIR)/%.o: src/harness/%.c | $(AMIGA_BUILD_DIR)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -c $< -o $@

ifeq ($(OS),Windows_NT)
clean:
	-cmd /C "del /F /Q $(TARGET) $(TARGET).exe $(STRING_POOL_TEST) $(STRING_POOL_TEST).exe $(PARSE_GROUP_KEY_TEST) $(PARSE_GROUP_KEY_TEST).exe 2>nul & del /F /S /Q src\\*.o 2>nul & del /F /Q ..\\..\\Bin\\Amiga\\varianttest 2>nul & del /F /S /Q ..\\..\\build\\amiga\\varianttest\\*.o 2>nul"
else
clean:
	-rm -f $(TARGET) $(TARGET).exe $(STRING_POOL_TEST) $(STRING_POOL_TEST).exe $(PARSE_GROUP_KEY_TEST) $(PARSE_GROUP_KEY_TEST).exe src/**/*.o $(AMIGA_BIN) $(AMIGA_BUILD_DIR)/**/*.o
endif
