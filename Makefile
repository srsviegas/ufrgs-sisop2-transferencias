
CLIENTDIR := client
SERVERDIR := server
BINDIR := bin

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
endif
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
  IS_WINDOWS := 1
endif
ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
  IS_WINDOWS := 1
endif

ifeq ($(IS_WINDOWS),1)
  EXEEXT := .exe
else
  EXEEXT :=
endif

CXX ?= g++
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++17
LDFLAGS ?=

SRC_EXT := cpp
SRCS_CLIENT := $(shell find $(CLIENTDIR) -type f -name '*.$(SRC_EXT)' 2>/dev/null)
SRCS_SERVER := $(shell find $(SERVERDIR) -type f -name '*.$(SRC_EXT)' 2>/dev/null)
OBJS_CLIENT := $(patsubst $(CLIENTDIR)/%,$(BINDIR)/obj/%,$(SRCS_CLIENT:.$(SRC_EXT)=.o))
OBJS_SERVER := $(patsubst $(SERVERDIR)/%,$(BINDIR)/obj/%,$(SRCS_SERVER:.$(SRC_EXT)=.o))

CLIENT_TARGET := $(BINDIR)/client$(EXEEXT)
SERVER_TARGET := $(BINDIR)/server$(EXEEXT)

TARGETS :=
ifneq ($(strip $(SRCS_CLIENT)),)
TARGETS += $(CLIENT_TARGET)
endif
ifneq ($(strip $(SRCS_SERVER)),)
TARGETS += $(SERVER_TARGET)
endif

.PHONY: all install clean dirs

ifeq ($(strip $(TARGETS)),)
all:
	@echo "No source files found in $(CLIENTDIR) or $(SERVERDIR). Expected *.$(SRC_EXT). Ajuste CLIENTDIR/SERVERDIR/SRC_EXT se necessário."
	@false
else
all: $(TARGETS)
endif

dirs:
ifeq ($(IS_WINDOWS),1)
	@echo "Detected Windows environment"
	@if not exist "$(BINDIR)" ( powershell -Command "New-Item -ItemType Directory -Force -Path '$(BINDIR)'" ) else ( exit 0 )
	@mkdir "$(BINDIR)\obj" 2>nul || true
else
	@mkdir -p $(BINDIR)/obj
endif

$(CLIENT_TARGET): dirs $(OBJS_CLIENT)
	@echo "Linking -> $@"
	$(CXX) $(LDFLAGS) -o $@ $(OBJS_CLIENT)

$(SERVER_TARGET): dirs $(OBJS_SERVER)
	@echo "Linking -> $@"
	$(CXX) $(LDFLAGS) -o $@ $(OBJS_SERVER)

$(BINDIR)/obj/%.o: $(CLIENTDIR)/%.$(SRC_EXT)
	@mkdir -p $(dir $@)
	@echo "Compiling $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BINDIR)/obj/%.o: $(SERVERDIR)/%.$(SRC_EXT)
	@mkdir -p $(dir $@)
	@echo "Compiling $< -> $@"
	$(CXX) $(CXXFLAGS) -c $< -o $@



install: all
	@echo "Installing binary to $(BINDIR)"
ifeq ($(IS_WINDOWS),1)
	@echo "Windows install - binaries em $(TARGETS)"
else
	@if [ -n "$(TARGETS)" ]; then chmod +x $(TARGETS) || true; fi
endif
	@echo "Done."

clean:
	@echo "Cleaning build artifacts..."
ifeq ($(IS_WINDOWS),1)
	@if exist "$(BINDIR)" ( rmdir /S /Q "$(BINDIR)" ) else ( exit 0 )
else
	@rm -rf $(BINDIR)
endif
	@echo "Clean complete."

info:
	@echo "CLIENTDIR = $(CLIENTDIR)"
	@echo "SERVERDIR = $(SERVERDIR)"
	@echo "BINDIR = $(BINDIR)"
	@echo "UNAME_S = $(UNAME_S)"
	@echo "IS_WINDOWS = $(IS_WINDOWS)"
	@echo "SRCS_CLIENT = $(SRCS_CLIENT)"
	@echo "SRCS_SERVER = $(SRCS_SERVER)"
	@echo "OBJS_CLIENT = $(OBJS_CLIENT)"
	@echo "OBJS_SERVER = $(OBJS_SERVER)"
	@echo "CLIENT_TARGET = $(CLIENT_TARGET)"
	@echo "SERVER_TARGET = $(SERVER_TARGET)"
	@echo "TARGETS = $(TARGETS)"
