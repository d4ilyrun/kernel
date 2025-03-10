LIBTEST_DIR       := $(LIB_DIR)/libtest

TESTS_CFLAGS   := -O0 -g3
TESTS_CPPFLAGS := $(CPPFLAGS)
TESTS_LDFLAGS  := -L$(BUILD_DIR)

CRITERION_DIR      := $(LIBTEST_DIR)/criterion
CRITERION_CFLAGS   := $(TESTS_CFLAGS)
CRITERION_CPPFLAGS := $(TESTS_CPPFLAGS) -I$(CRITERION_DIR)/include
CRITERION_LDFLAGS  := $(TESTS_LDFLAGS) -lcriterion

define DEFINE_CRITERION_TESTSUITE

$(1)_TESTS_DIR := $$($(1)_DIR)/tests
$(1)_TESTS    := $(foreach test,$(2),tests/$(1)/$(test))

$$(BUILD_DIR)/$(1)/tests/%: CRITERION_CPPFLAGS += -I$$($(1)_DIR)/include
$$(BUILD_DIR)/$(1)/tests/%: CRITERION_LDFLAGS  += $$($(1)_TESTS_LDFLAGS)
$$(BUILD_DIR)/$(1)/tests/%: $$($(1)_TESTS_DIR)/%.c $(1)
	$$(call COMPILE,CC,$$@)
	$$(SILENT)$$(CC) $$(CRITERION_CPPFLAGS) $$(CRITERION_CFLAGS) $$< -o $$@ $$(CRITERION_LDFLAGS)

tests/$(1)/%: $$(BUILD_DIR)/$(1)/tests/%
	$$(call LOG,TEST,$$@)
	$$(SILENT)$$<

.PHONY: tests/$(1)
tests/$(1): $$($(1)_TESTS)

TESTS += tests/$(1)

endef

TESTS :=
