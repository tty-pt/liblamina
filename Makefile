BE := glfw

INPUT-fb := dev
INPUT-glfw := glfw

LDLIBS-Linux += -lGL -lX11 -lGLEW -lglfw
LDLIBS-OpenBSD += -lGL -lGLU -lX11 -lGLEW -lglfw
LDLIBS-Windows += -lglew32 -lglfw3 -lopengl32 -lgdi32 -luser32 -lkernel32
LDLIBS-Darwin += -lglfw

LDFLAGS-Darwin += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

add-prefix-OpenBSD += /usr/X11R6

LDLIBS := -lm -lxxhash -lcorm -lstele -lpng
LDLIBS += ${LDLIBS-${BE}}

LDLIBS-Linux += -lEGL
LDLIBS-Linux += -Wl,--no-as-needed -lstele -Wl,--as-needed
LDLIBS += ${LDLIBS-${uname}}

obj-y := glfw img png
obj-y += tile font
obj-y += ui ui-style ui-cache shadow
obj-y += input input-glfw
liblamina-obj-y := ${obj-y:%=src/%.o}
posix := fb input-dev
liblamina-obj-y-Linux := ${posix:%=src/%.o}
# liblamina-obj-y-OpenBSD := ${posix:%=src/%.o}

CFLAGS := -g
all := liblamina

-include ../mk/include.mk

${DESTDIR}${PREFIX}/lib/pkgconfig/lamina.pc: lamina.pc
	install -d ${DESTDIR}${PREFIX}/lib/pkgconfig
	install -m 644 lamina.pc $@

install: ${DESTDIR}${PREFIX}/lib/pkgconfig/lamina.pc

# Test configuration
TEST_DIR := tests
TEST_CFLAGS := -I${PWD}/include
TEST_LDFLAGS := -L${PWD}/lib
TEST_LDLIBS := -llamina ${LDLIBS}

# Test binaries
TEST_BINS := ${TEST_DIR}/test_core${EXE} \
	${TEST_DIR}/test_textures${EXE} \
	${TEST_DIR}/test_fonts${EXE} \
	${TEST_DIR}/test_tilemaps${EXE} \
	${TEST_DIR}/test_ui_layout${EXE} \
	${TEST_DIR}/test_ui_style${EXE} \
	${TEST_DIR}/test_ui_render${EXE} \
	${TEST_DIR}/test_integration${EXE}

${TEST_DIR}:
	@mkdir -p ${TEST_DIR}/fixtures 2>/dev/null || true

${TEST_DIR}/fixtures/test_texture.png:
	@mkdir -p ${TEST_DIR}/fixtures 2>/dev/null || true
	@python3 ${TEST_DIR}/generate_fixtures.py 2>/dev/null || true

${TEST_DIR}/test_core${EXE}: ${TEST_DIR} ${TEST_DIR}/test_core.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_core.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_textures${EXE}: ${TEST_DIR} ${TEST_DIR}/test_textures.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_textures.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_fonts${EXE}: ${TEST_DIR} ${TEST_DIR}/test_fonts.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_fonts.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_tilemaps${EXE}: ${TEST_DIR} ${TEST_DIR}/test_tilemaps.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_tilemaps.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_ui_layout${EXE}: ${TEST_DIR} ${TEST_DIR}/test_ui_layout.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_ui_layout.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_ui_style${EXE}: ${TEST_DIR} ${TEST_DIR}/test_ui_style.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_ui_style.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_ui_render${EXE}: ${TEST_DIR} ${TEST_DIR}/test_ui_render.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_ui_render.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

${TEST_DIR}/test_integration${EXE}: ${TEST_DIR} ${TEST_DIR}/test_integration.c lib/liblamina.${SO}
	${cc} -o $@ ${TEST_DIR}/test_integration.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} ${TEST_LDFLAGS} ${TEST_LDLIBS}

test-build: lib/liblamina.${SO} ${TEST_DIR}/fixtures/test_texture.png ${TEST_BINS}

test: test-build
	@if [ -z "$$DISPLAY" ] && [ ! -e /dev/fb0 ] && command -v xvfb-run >/dev/null 2>&1 && command -v Xvfb >/dev/null 2>&1; then \
		xvfb-run -a -s "-screen 0 1024x768x24" $(MAKE) run-tests; \
	else \
		$(MAKE) run-tests; \
	fi

run-tests:
	@echo "Running test_core..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_core
	@echo "Running test_textures..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_textures
	@echo "Running test_fonts..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_fonts
	@echo "Running test_tilemaps..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_tilemaps
	@echo "Running test_ui_layout..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_ui_layout
	@echo "Running test_ui_style..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_ui_style
	@echo "Running test_ui_render..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_ui_render
	@echo "Running test_integration..."
	@LD_LIBRARY_PATH=./lib ./${TEST_DIR}/test_integration
	@echo "All tests passed!"

.PHONY: test test-build run-tests

# Examples configuration
EXAMPLES_DIR := examples
EXAMPLES_CFLAGS := -I${PWD}/include
EXAMPLES_LDFLAGS := -L${PWD}/lib
EXAMPLES_LDLIBS := -llamina ${LDLIBS}

# Example binaries
EXAMPLE_BINS := ${EXAMPLES_DIR}/01_basic_rendering${EXE} \
	${EXAMPLES_DIR}/02_textures${EXE} \
	${EXAMPLES_DIR}/03_fonts${EXE} \
	${EXAMPLES_DIR}/04_tilemaps${EXE} \
	${EXAMPLES_DIR}/05_ui_layout${EXE} \
	${EXAMPLES_DIR}/06_ui_advanced${EXE}

${EXAMPLES_DIR}:
	@mkdir -p ${EXAMPLES_DIR} 2>/dev/null || true

${EXAMPLES_DIR}/01_basic_rendering${EXE}: ${EXAMPLES_DIR} ${EXAMPLES_DIR}/01_basic_rendering.c lib/liblamina.${SO}
	${cc} -o $@ ${EXAMPLES_DIR}/01_basic_rendering.c ${CFLAGS} ${EXAMPLES_CFLAGS} \
		${LDFLAGS} ${EXAMPLES_LDFLAGS} ${EXAMPLES_LDLIBS}

${EXAMPLES_DIR}/02_textures${EXE}: ${EXAMPLES_DIR} ${EXAMPLES_DIR}/02_textures.c lib/liblamina.${SO}
	${cc} -o $@ ${EXAMPLES_DIR}/02_textures.c ${CFLAGS} ${EXAMPLES_CFLAGS} \
		${LDFLAGS} ${EXAMPLES_LDFLAGS} ${EXAMPLES_LDLIBS}

${EXAMPLES_DIR}/03_fonts${EXE}: ${EXAMPLES_DIR} ${EXAMPLES_DIR}/03_fonts.c lib/liblamina.${SO}
	${cc} -o $@ ${EXAMPLES_DIR}/03_fonts.c ${CFLAGS} ${EXAMPLES_CFLAGS} \
		${LDFLAGS} ${EXAMPLES_LDFLAGS} ${EXAMPLES_LDLIBS}

${EXAMPLES_DIR}/04_tilemaps${EXE}: ${EXAMPLES_DIR} ${EXAMPLES_DIR}/04_tilemaps.c lib/liblamina.${SO}
	${cc} -o $@ ${EXAMPLES_DIR}/04_tilemaps.c ${CFLAGS} ${EXAMPLES_CFLAGS} \
		${LDFLAGS} ${EXAMPLES_LDFLAGS} ${EXAMPLES_LDLIBS}

${EXAMPLES_DIR}/05_ui_layout${EXE}: ${EXAMPLES_DIR} ${EXAMPLES_DIR}/05_ui_layout.c lib/liblamina.${SO}
	${cc} -o $@ ${EXAMPLES_DIR}/05_ui_layout.c ${CFLAGS} ${EXAMPLES_CFLAGS} \
		${LDFLAGS} ${EXAMPLES_LDFLAGS} ${EXAMPLES_LDLIBS}

${EXAMPLES_DIR}/06_ui_advanced${EXE}: ${EXAMPLES_DIR} ${EXAMPLES_DIR}/06_ui_advanced.c lib/liblamina.${SO}
	${cc} -o $@ ${EXAMPLES_DIR}/06_ui_advanced.c ${CFLAGS} ${EXAMPLES_CFLAGS} \
		${LDFLAGS} ${EXAMPLES_LDFLAGS} ${EXAMPLES_LDLIBS}

examples: lib/liblamina.${SO} ${EXAMPLE_BINS}

.PHONY: examples
