# Window Sweaters for Hyprland.
#
#   make            build window-sweaters.so (needs Hyprland's headers: pkg-config hyprland)
#   make test       run the renderer's unit tests (needs only Cairo)
#   make plates     render the collection plates into build/plates
#   make compare    compare those plates with the macOS renderer's (docs/collection; Python 3, Pillow, NumPy)

CC  ?= cc
CXX ?= g++
CFLAGS   ?= -O2
CXXFLAGS ?= -O2

CORE = core/knit.c core/chart.c core/apps.c core/paths.c core/icons.c core/autoyarn.c
CORE_OBJ = $(patsubst core/%.c,build/core/%.o,$(CORE))
PLUGIN = plugin/main.cpp plugin/KnitDecoration.cpp plugin/KnitPassElement.cpp

# SVG icons (most app icons on Linux) are sampled with librsvg when it is
# installed; without it, only PNG icons give apps their colour.
ifeq ($(shell pkg-config --exists librsvg-2.0 && echo yes),yes)
  RSVG_CFLAGS = -DKNIT_HAVE_RSVG $(shell pkg-config --cflags librsvg-2.0)
  RSVG_LIBS   = $(shell pkg-config --libs librsvg-2.0)
endif

CAIRO_CFLAGS = $(shell pkg-config --cflags cairo)
CAIRO_LIBS   = $(shell pkg-config --libs cairo)
# Icon paths longer than PATH_MAX are simply not found, so truncation is fine.
CORE_CFLAGS  = -std=c11 -D_GNU_SOURCE -fPIC -Wall -Wextra -Wno-format-truncation $(CAIRO_CFLAGS) $(RSVG_CFLAGS)

# g++ marks inline statics as unique symbols, which stops a plugin unloading.
ifeq ($(findstring g++,$(CXX)),g++)
  EXTRA_FLAGS = --no-gnu-unique
endif

.PHONY: all clean test plates compare

all: window-sweaters.so

build/core/%.o: core/%.c core/*.h | build/core
	$(CC) $(CFLAGS) $(CORE_CFLAGS) -c $< -o $@

build/core:
	mkdir -p $@

window-sweaters.so: $(PLUGIN) plugin/*.hpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -shared -fPIC -std=c++2b $(EXTRA_FLAGS) $(PLUGIN) $(CORE_OBJ) -o $@ \
	  `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon` \
	  $(CAIRO_LIBS) $(RSVG_LIBS) -lpthread -lm

build/core-test: tests/core_test.c $(CORE) core/*.h | build/core
	$(CC) -O1 -g -fsanitize=address,undefined $(CORE_CFLAGS) tests/core_test.c $(CORE) -o $@ $(CAIRO_LIBS) $(RSVG_LIBS) -lpthread -lm

build/render-test: tests/render.c tests/catalogue.h $(CORE) core/*.h | build/core
	$(CC) -O2 $(CORE_CFLAGS) tests/render.c $(CORE) -o $@ $(CAIRO_LIBS) $(RSVG_LIBS) -lpthread -lm

test: build/core-test
	build/core-test

plates: build/render-test
	mkdir -p build/plates
	build/render-test --individual build/plates
	build/render-test --plain build/plain.png
	build/render-test --desktop build/desktop.png

compare: plates
	python3 tests/compare.py ../docs/collection build/plates

clean:
	rm -rf build window-sweaters.so
