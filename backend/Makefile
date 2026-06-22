CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
LDFLAGS := -lcurl -lm -lwayland-client
TARGET := ../bin/voxflow-backend

SRCDIR := src
VENDORDIR := vendor
BUILDDIR := build

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

CXXFLAGS += -I$(SRCDIR) -I$(VENDORDIR)

.PHONY: all clean vendor

all: vendor $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

vendor: $(VENDORDIR)/miniaudio.h $(VENDORDIR)/nlohmann/json.hpp

$(VENDORDIR)/miniaudio.h:
	@echo "Downloading miniaudio.h..."
	@curl -sL -o $@ "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"

$(VENDORDIR)/nlohmann/json.hpp:
	@mkdir -p $(VENDORDIR)/nlohmann
	@echo "Downloading json.hpp..."
	@curl -sL -o $@ "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp"

clean:
	rm -rf $(BUILDDIR) $(TARGET)

-include $(DEPS)
