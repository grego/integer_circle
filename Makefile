CFLAGS=-Wall -Wextra -Wno-unused -march=native -O2 -flto=auto
LDFLAGS=-lX11 -lXi -lXcursor -lGL -lasound -ldl -lm
WASM_CFLAGS=-Wall -Wextra -Wno-unused -Os -flto
WASM_LDFLAGS=-sUSE_WEBGL2=1 -sASSERTIONS=0 -sMALLOC=emmalloc --closure=1

integer_circle: integer_circle.c sokol.o
	cc $^ ${LDFLAGS} ${CFLAGS} -o $@

sokol.o: sokol/sokol.c
	cc $^ -c ${CFLAGS}

integer_circle.js: integer_circle.c sokol_wasm.o
	emcc $^ ${WASM_LDFLAGS} ${WASM_CFLAGS} --embed-file frag.glsl -o $@

sokol_wasm.o: sokol/sokol.c
	emcc $^ ${WASM_CFLAGS} -c -o $@
