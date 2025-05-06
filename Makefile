#########################################################
# 			Directories									#
#########################################################

MINGW := $(shell uname -a | grep MINGW | wc -l)
PLATFORM := $(shell uname -s)

#INC_DIR := ./inc
SRC_DIR := ./src
BIN_DIR := ./bin
LIB_DIR := ./lib
BUILD_DIR := ./build/$(PLATFORM)
DEP_DIR := $(BUILD_DIR)/dep
OBJ_DIR := $(BUILD_DIR)/obj

# package directories
ELOG_DIR := $(SRC_DIR)/elog

# include directories
ELOG_INC_DIR := $(ELOG_DIR)/inc

# source directories
ELOG_SRC_DIR := $(ELOG_DIR)/src

# object directories
ELOG_OBJ_DIR := $(OBJ_DIR)/elog

ELOG_OBJ_DIR_STATIC := $(ELOG_OBJ_DIR)/static
STATIC_OBJ_DIRS := $(ELOG_OBJ_DIR_STATIC)

ELOG_OBJ_DIR_DYNAMIC := $(ELOG_OBJ_DIR)/dynamic
DYNAMIC_OBJ_DIRS := $(ELOG_OBJ_DIR_DYNAMIC)

OBJ_DIRS := $(STATIC_OBJ_DIRS) $(DYNAMIC_OBJ_DIRS)

# dependency directories
ELOG_DEP_DIR := $(DEP_DIR)/elog

ELOG_DEP_DIR_STATIC := $(ELOG_DEP_DIR)/static
STATIC_DEP_DIRS := $(ELOG_DEP_DIR_STATIC)

ELOG_DEP_DIR_DYNAMIC := $(ELOG_DEP_DIR)/dynamic
DYNAMIC_DEP_DIRS := $(ELOG_DEP_DIR_DYNAMIC)

DEP_DIRS := $(STATIC_DEP_DIRS) $(DYNAMIC_DEP_DIRS)

# install directories
ELOG_HOME := $(INSTALL_DIR)/elog
ELOG_INSTALL_DIR := $(ELOG_HOME)/include/elog
INSTALL_BIN := $(INSTALL_DIR)/bin
INSTALL_LIB := $(INSTALL_DIR)/lib

INSTALL_DIRS := $(ELOG_INSTALL_DIR) $(INSTALL_BIN) $(INSTALL_LIB)


#########################################################
# 			Header Files								#
#########################################################

# common header files
ELOG_HDRS := $(shell find $(ELOG_INC_DIR) -type f -name '*.h')

HDRS := $(ELOG_HDRS)

#########################################################
# 			Source Files								#
#########################################################

# common source files
ELOG_SRCS := $(shell find $(ELOG_SRC_DIR) -type f -name '*.cpp')

SRCS := $(ELOG_SRCS)


#########################################################
# 			Object Files								#
#########################################################

# object files
ELOG_OBJS := $(ELOG_SRCS:.cpp=.o)
ELOG_OBJS_STATIC := $(patsubst $(ELOG_SRC_DIR)/%,$(ELOG_OBJ_DIR_STATIC)/%,$(ELOG_OBJS))
ELOG_OBJS_DYNAMIC := $(patsubst $(ELOG_SRC_DIR)/%,$(ELOG_OBJ_DIR_DYNAMIC)/%,$(ELOG_OBJS))

OBJS_STATIC := $(ELOG_OBJS_STATIC)
OBJS_DYNAMIC := $(ELOG_OBJS_DYNAMIC)
OBJS := $(OBJS_STATIC) $(OBJS_DYNAMIC)


#########################################################
# 			Dependency Files							#
#########################################################

# dependency files
ELOG_DEPS := $(ELOG_SRCS:.cpp=.dep)
ELOG_DEPS_STATIC := $(patsubst $(ELOG_SRC_DIR)/%,$(ELOG_DEP_DIR_STATIC)/%,$(ELOG_DEPS))
ELOG_DEPS_DYNAMIC := $(patsubst $(ELOG_SRC_DIR)/%,$(ELOG_DEP_DIR_DYNAMIC)/%,$(ELOG_DEPS))

DEPS_STATIC := $(ELOG_DEPS_STATIC)
DEPS_DYNAMIC := $(ELOG_DEPS_DYNAMIC)
DEPS := $(DEPS_STATIC) $(DEPS_DYNAMIC)


#########################################################
# 			Install Files						    	#
#########################################################

# object files
ELOG_INSTALL_FILES := $(patsubst $(ELOG_INC_DIR)/%,$(ELOG_INSTALL_DIR)/%,$(ELOG_HDRS))

INSTALL_FILES := $(ELOG_INSTALL_FILES)


#########################################################
# 			Targets										#
#########################################################

#names
ELOG_LIB_NAME := libelog.a
ifeq ($(MINGW), 1)
ELOG_DLL_NAME := libelog.dll
else
ELOG_DLL_NAME := libelog.so
endif


# targets
ELOG_LIB := $(LIB_DIR)/$(ELOG_LIB_NAME)
ELOG_DLL := $(BIN_DIR)/$(ELOG_DLL_NAME)

# build targets
all: elog_dll elog_lib

elog_dll: dirs $(ELOG_DLL)

elog_lib: dirs $(ELOG_LIB)

# install targets
INSTALL_ELOG_LIB := $(INSTALL_LIB)/$(ELOG_LIB_NAME)
INSTALL_ELOG_DLL := $(INSTALL_BIN)/$(ELOG_DLL_NAME)
INSTALL_TARGETS := $(INSTALL_ELOG_LIB) $(INSTALL_ELOG_DLL)

dirs: $(LIB_DIR) $(BIN_DIR) $(OBJ_DIR) $(OBJ_DIRS) $(DEP_DIRS) $(INSTALL_DIRS)

clean:
	-rm -f $(ELOG_LIB)
	-rm -f $(ELOG_DLL)
	-rm -f $(OBJS)
	-rm -f $(DEPS)
#	-rm -f $(TEST_OBJS)

install: all $(INSTALL_FILES) $(INSTALL_TARGETS)


#########################################################
# 			Compile Rules								#
#########################################################

# compiler
CPP := g++

# compilation flags
CPPFLAGS := -std=c++23 -g3

# project include path
CPPFLAGS += -I. -I$(ELOG_INC_DIR)

# special MinGW include dirs
ifeq ($(MINGW), 1)
	CPPFLAGS += -I/ucrt64/include
endif

# link flags
#DEP_FLAGS := -MT $@ -MMD -MP -MF $(patsubst $(OBJ_DIR)/%.o,$(DEP_DIR)/%.tmp.dep,$@)
#LDFLAGS := -L$(BIN_DIR)
ifeq ($(MINGW), 1)
	LDFLAGS := $(LDFLAGS) -L/ucrt64/lib -lws2_32
else
	LDFLAGS := $(LDFLAGS) -rdynamic
endif

# add sqlite 3 link flags
ifeq ($(ELOG_ENABLE_SQLITE_DB_CONNECTOR), 1)
	CPPFLAGS += -DELOG_ENABLE_SQLITE_DB_CONNECTOR
	LDFLAGS += -lsqlite3
endif

# add PostgreSQL flags
ifeq ($(ELOG_ENABLE_PGSQL_DB_CONNECTOR), 1)
	CPPFLAGS += -DELOG_ENABLE_PGSQL_DB_CONNECTOR
	LDFLAGS += -lpq
#ifeq (($PLATFORM), Linux)
	CPPFLAGS += -I/usr/include/postgresql
#endif
endif
#POST_COMPILE = mv -f $(DEP_DIR)/$*.tmp.dep $(DEP_DIR)/$*.dep && touch $@

# directory targets
$(LIB_DIR): ; mkdir -p $@
$(BIN_DIR): ; mkdir -p $@
$(OBJ_DIR): ; mkdir -p $@
$(DEP_DIR): ; mkdir -p $@
$(INSTALL_DIR): ; mkdir -p $@
$(ELOG_OBJ_DIR_STATIC): ; mkdir -p $@
$(ELOG_OBJ_DIR_DYNAMIC): ; mkdir -p $@
$(ELOG_DEP_DIR_STATIC): ; @mkdir -p $@
$(ELOG_DEP_DIR_DYNAMIC): ; @mkdir -p $@
$(ELOG_INSTALL_DIR): ; mkdir -p $@
$(INSTALL_BIN): ; mkdir -p $@
$(INSTALL_LIB): ; mkdir -p $@

# binary targets
$(ELOG_LIB): $(OBJS_STATIC)
	ar rcs -o $@ $(OBJS_STATIC)

$(ELOG_DLL): $(OBJS_DYNAMIC)
	$(CPP) $(OBJS_DYNAMIC) $(LDFLAGS) -shared -o $@

# make sure all object files depend on dependency files like this:
# %.o: %.cpp %.dep
#	$(MAKE_DEPEND)
#	$(BUILD_CMD)
#
# in addition, at the end of the file we have empty target with no recipe:
# $(DEPS):
#
# this way, if a dependency file is missing, it needs to be rebuilt, but there is no recipe, so nothing will happened,
# BUT, the depending target will be declared as out of date, even if the .o file is already up-to-date (because it depends
# on a missing file with an empty recipe).
# the result of this is invocation of the recipe, which contains a $(MAKE_DEPEND) command which generates
# the dependency file.
# the final include of all dependencies will cause another re-execution of the Makefile (as needed).

# object targets
$(ELOG_OBJ_DIR_STATIC)/%.o : $(ELOG_SRC_DIR)/%.cpp $(ELOG_DEP_DIR_STATIC)/%.dep | $(ELOG_DEP_DIR_STATIC)
	$(CPP) -MT $@ -MMD -MP -MF $(ELOG_DEP_DIR_STATIC)/$*.dep $(CPPFLAGS) -c $< -o $@

$(ELOG_OBJ_DIR_DYNAMIC)/%.o : $(ELOG_SRC_DIR)/%.cpp $(ELOG_DEP_DIR_DYNAMIC)/%.dep | $(ELOG_DEP_DIR_DYNAMIC)
	$(CPP) -fPIC -MT $@ -MMD -MP -MF $(ELOG_DEP_DIR_DYNAMIC)/$*.dep $(CPPFLAGS) -c $< -o $@

# install targets
$(ELOG_INSTALL_DIR)/%.h: $(ELOG_INC_DIR)/%.h
	cp $< $@

$(INSTALL_ELOG_DLL): $(ELOG_DLL)
	cp $< $@

$(INSTALL_ELOG_LIB): $(ELOG_LIB)
	cp $< $@

# add empty target for all dependency files
$(DEPS):

include $(DEPS)

# for elaborate explanation about makedepend stuff see:
# https://make.mad-scientist.net/papers/advanced-auto-dependency-generation/