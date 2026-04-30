CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic
LDFLAGS ?=

TARGET := variant_harness
SRCS := src/main.c \
	src/vh_csv.c \
	src/vh_fields.c \
	src/vh_memtrack.c \
	src/vh_parse.c \
	src/vh_profile.c \
	src/vh_score.c \
	src/vh_group.c \
	src/vh_string_pool.c
OBJS := $(SRCS:.c=.o)
STRING_POOL_TEST := tests/vh_string_pool_test
PARSE_GROUP_KEY_TEST := tests/vh_parse_group_key_test

AMIGA_CC ?= vc
AMIGA_AUTO ?= 1

AMIGA_BUILD_DIR := ../../build/amiga/varianttest
AMIGA_BIN := ../../Bin/Amiga/varianttest

AMIGA_SRCS := src/amiga_varianttest.c \
	src/vh_amiga_builtin_compat.c \
	src/vh_csv.c \
	src/vh_memtrack.c \
	src/vh_parse.c \
	src/vh_profile.c \
	src/vh_score.c \
	src/vh_group.c \
	src/vh_string_pool.c
AMIGA_OBJS := $(patsubst src/%.c,$(AMIGA_BUILD_DIR)/%.o,$(AMIGA_SRCS))

ROADSHOW_INC ?= C:/Amiga/Roadshow-SDK-1.8/netinclude
NDK_INC ?= C:/Amiga/AmigaIncludes

AMIGA_CFLAGS := +aos68k -c99 -cpu=68000 -O2 -size \
	-I$(VBCC)/targets/m68k-amigaos/include \
	-Isrc \
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
AMIGA_MKDIR_BUILD = powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '../../build/amiga/varianttest' | Out-Null"
AMIGA_MKDIR_BIN = powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '../../Bin/Amiga' | Out-Null"
else
AMIGA_MKDIR_BUILD = mkdir -p ../../build/amiga/varianttest
AMIGA_MKDIR_BIN = mkdir -p ../../Bin/Amiga
endif

.PHONY: all clean test varianttest-amiga

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
ifeq ($(OS),Windows_NT)
	$(CC) $(CFLAGS) tests/test_vh_string_pool.c src/vh_string_pool.c src/vh_memtrack.c -o $(STRING_POOL_TEST)
	$(CC) $(CFLAGS) tests/test_vh_parse_group_key.c src/vh_parse.c src/vh_csv.c src/vh_string_pool.c src/vh_memtrack.c -o $(PARSE_GROUP_KEY_TEST)
	powershell -NoProfile -Command "$$p='$(STRING_POOL_TEST).exe'; if (!(Test-Path -LiteralPath $$p)) { $$p='$(STRING_POOL_TEST)' }; $$p=(Resolve-Path -LiteralPath $$p).Path; & $$p; if ($$LASTEXITCODE -ne 0) { exit $$LASTEXITCODE }"
	powershell -NoProfile -Command "$$p='$(PARSE_GROUP_KEY_TEST).exe'; if (!(Test-Path -LiteralPath $$p)) { $$p='$(PARSE_GROUP_KEY_TEST)' }; $$p=(Resolve-Path -LiteralPath $$p).Path; & $$p; if ($$LASTEXITCODE -ne 0) { exit $$LASTEXITCODE }"
	powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_milestone4_tests.ps1
else
	$(CC) $(CFLAGS) tests/test_vh_string_pool.c src/vh_string_pool.c src/vh_memtrack.c -o $(STRING_POOL_TEST)
	$(CC) $(CFLAGS) tests/test_vh_parse_group_key.c src/vh_parse.c src/vh_csv.c src/vh_string_pool.c src/vh_memtrack.c -o $(PARSE_GROUP_KEY_TEST)
	./$(STRING_POOL_TEST)
	./$(PARSE_GROUP_KEY_TEST)
	./$(TARGET) --select tests/filenames_basic.txt --profile default > test_output/basic_select.txt
	./$(TARGET) --report tests/filenames_basic.txt --profile default > test_output/basic_report.txt
	./$(TARGET) --report tests/filenames_memory.txt --profile default > test_output/memory_report.txt
	./$(TARGET) --report tests/filenames_language.txt --profile default > test_output/language_report.txt
	./$(TARGET) --report tests/filenames_special_report_only.txt --profile default > test_output/special_report_only.txt
endif

varianttest-amiga: $(AMIGA_BIN)

$(AMIGA_BIN): $(AMIGA_OBJS)
	$(AMIGA_MKDIR_BIN)
	$(AMIGA_CC) $(AMIGA_LDFLAGS) -o $@ $^

$(AMIGA_BUILD_DIR)/%.o: src/%.c
	$(AMIGA_MKDIR_BUILD)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -c $< -o $@

ifeq ($(OS),Windows_NT)
clean:
	-cmd /C "del /F /Q $(TARGET) $(TARGET).exe $(STRING_POOL_TEST) $(STRING_POOL_TEST).exe $(PARSE_GROUP_KEY_TEST) $(PARSE_GROUP_KEY_TEST).exe 2>nul & del /F /Q src\\*.o 2>nul & del /F /Q ..\\..\\Bin\\Amiga\\varianttest 2>nul & del /F /Q ..\\..\\build\\amiga\\varianttest\\*.o 2>nul"
else
clean:
	-rm -f $(TARGET) $(TARGET).exe $(STRING_POOL_TEST) $(STRING_POOL_TEST).exe $(PARSE_GROUP_KEY_TEST) $(PARSE_GROUP_KEY_TEST).exe src/*.o $(AMIGA_BIN) $(AMIGA_BUILD_DIR)/*.o
endif
