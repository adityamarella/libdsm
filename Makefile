AR = $(CROSS_COMPILE)ar
CC = $(CROSS_COMPILE)gcc$(CC_VERSION)

OBJ_DIR = obj
SRC_DIR = src
INCLUDE_DIR = include
BIN_DIR = bin
THIRDPARTY_DIR = thirdparty
LIB_DIR = lib
TEST_DIR = test/src
CONF_FILE = dsm.conf
UNAME = $(shell uname)

ifeq ($(UNAME),Linux)
  NANOMSG_LDFLAGS = -lanl -lrt $(shell pkg-config libbsd --libs)
else
  NANOMSG_LDFLAGS =
endif

ifeq ($(DEBUG), 1)
	CFLAGS=-DDEBUG
else
	CFLAGS=-DNDEBUG
endif

THIRDPARTY_LIB_FLAGS = -lnanomsg

LDFLAGS = -ggdb
ARFLAGS = -r
CCFLAGS = -ggdb -Wall -Wextra -Werror -Wswitch-default -Wwrite-strings \
	-O3 -Iinclude -Itest/include -std=gnu99 $(CFLAGS) -x c

DSM_SRCS = dsm.c dsm_conf.c dsm_internal.c reply_handler.c request.c strings.c comm.c dsm_server.c
DSM_OBJS = $(DSM_SRCS:%.c=$(OBJ_DIR)/%.o)

TEST_SRCS = main.c test_matrix_mul.c
TEST_OBJS = $(TEST_SRCS:%.c=$(OBJ_DIR)/%.o)

MATRIX_MUL_SRCS = test_matrix_mul.c
MATRIX_MUL_OBJS = $(MATRIX_MUL_SRCS:%.c=$(OBJ_DIR)/%.o)


LIB_NAME = dsm
LIB = $(LIB_DIR)/lib$(LIB_NAME).a
TEST_BIN = $(BIN_DIR)/test

.PHONY: all clean test test1 test2 test3 matrix_gen matrix_mul fault

vpath % $(SRC_DIR) $(TEST_DIR)

all: $(LIB) test test1 test2 test3 matrix_gen matrix_mul fault

$(OBJ_DIR)/%.o: %.c $(EXTERN_INCLUDES)
	@mkdir -p $(@D)
	$(CC) $(CCFLAGS) -c $< -o $@

$(LIB): $(DSM_OBJS) 
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

$(TEST_BIN): $(TEST_OBJS) $(LIB)
	@mkdir -p $(@D)
	@cp $(CONF_FILE) $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) -L$(LIB_DIR) -ldsm -pthread $(THIRDPARTY_LIB_FLAGS)

test: $(TEST_BIN)
#	@$(TEST_BIN) -v

$(MATRIX_MUL_BIN): $(MATRIX_MUL_OBJS) $(LIB)
	@mkdir -p $(@D)
	@cp $(CONF_FILE) $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) -L$(LIB_DIR) -ldsm -pthread $(THIRDPARTY_LIB_FLAGS)

matrix_mul: $(MATRIX_MUL_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR) a b c d e f g h
	find -name "*~" | xargs rm -f

superclean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR) \
	  $(THIRDPARTY_DIR)/$(INCLUDE_DIR) a b c
