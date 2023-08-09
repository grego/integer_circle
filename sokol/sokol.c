#define SOKOL_IMPL
#if defined(__EMSCRIPTEN__)
#define SOKOL_GLES3
#else
#define SOKOL_GLCORE33
#endif
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_gl.h"
#include "sokol_debugtext.h"
#include "sokol_audio.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#define RFFT_H
#define RFFT_IMPLEMENTATION
#include "rfft.h"
