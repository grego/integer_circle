#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>

#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_gl.h"
#include "sokol/sokol_audio.h"
#include "sokol/sokol_debugtext.h"
#include "sokol/sokol_log.h"
#include "sokol/rfft.h"

#define MAX_FREQ 3200
#define MAX_ITERS 16384

typedef struct {
	float x;
	float y;
} point_t;

typedef struct {
	point_t resolution;
	point_t cam;
	float zoom;
	float delta;
	float epsilon;
	point_t p;
	uint32_t view;
	uint32_t color;
} params_t;

typedef struct {
	params_t params;
	float other_zoom;
	float audio_buffer[16384];
	point_t play_pt;
	float volume;
	float start_volume;
	bool dampen;
	float octave;
	bool move;
	point_t pointer;
	bool show_info;
	bool show_help;
	bool params_changed;
	bool smooth_change;
	point_t orbit[MAX_ITERS];
	float complex spectrum[MAX_ITERS];
	size_t orbit_len;
	float radius;
	struct {
    		point_t p;
    		float delta;
    		float epsilon;
    		float radius;
    		float volume;
	} old;
	struct {
		sg_pipeline pip;
		sg_bindings bind;
		sg_pass_action pass_action;
		sgl_pipeline sgl_alpha_pip;
	} gfx;
} state_t;

static state_t state = (state_t){
	.params = {
		.cam = { 0, 0 },
		.zoom = 1.0,
		.delta = 0.5,
		//.epsilon = 2*sin(M_PI/INIT_P)*sin(M_PI/INIT_P)/0.5,
		.epsilon = 1.381966, // pre-computed
		.p = { 0, 0 },
		.view = 1,
		.color = 0,
	},
	.other_zoom = 5000,
	.volume = 1.0,
	.start_volume = 1.0,
	.dampen = false,
	.octave = 1.0,
	.move = false,
	.show_info = false,
	.show_help = true,
	.params_changed = false,
	.smooth_change = false,
	.old = {
		.p = { 0, 0 },
		.volume = 0.0,
	}
};

const char *VERTEX_SHADER = "#version 100\n"
	"attribute vec4 pos;"
	"void main() { gl_Position = pos; }";

const char *HELP = "Left mouse - click to hear orbits\n"
		   "Middle/Shift + mouse - drag the view\n"
		   "Right mouse - toggle x/y and d/e view\n"
		   "Scroll wheel - zoom\n\n"
		   "H - toggle this help screen\n"
		   "I - toggle info screen\n"
		   "R - reset view\n"
		   "M - toggle moving along the period\n"
		   "C - change the color scheme\n\n"
		   "Space - stop the audio\n"
		   "D - toggle audio dampening\n\n"
		   "Keyboard:\n"
		   "1   2   3   4   5   6   7   8   9   0\n"
		   "F   G   G#  A   A#  C   D   D#  E   F\n"
		   "E/. - increase by octave\n"
		   "W/, - decrease by octave";

/// Ratios of the musical notes in just intonation
const float NOTES[10] = { 4.0/6.0, 3.0/4.0, 8.0/10.0, 5.0/6.0, 9.0/10.0,
                           1.0, 9.0/8.0, 6.0/5.0, 5.0/4.0, 4.0/3.0};

/// One iteration of the integer circle algorithm
point_t ic_iter(point_t p, const float delta, const float epsilon) {
	p.x -= floor(delta * p.y);
	p.y += floor(epsilon * p.x);
	p.x -= floor(delta * p.y);
	return p;
}

/// Calculate the period of oscillation if no flooring was done
float calculate_period(float delta, float epsilon) {
    return M_PI / asin(sqrt(delta*epsilon/2));
}

/// Update the parameters to keep the period constant
void update_parameter(float* delta, float* epsilon, float change) {
    const float product = *delta * *epsilon;
    *delta += change;
    *epsilon = product / *delta;
}

/// Calculate the second parameter with the given period
float other_parameter(float period, float delta) {
    const float s = sin(M_PI / period);
    return 2*s*s/delta;
}

point_t floor_pt(const point_t p) {
	return (point_t){
		.x = floor(p.x),
		.y = floor(p.y),
	};
}

point_t scale_pt(const point_t p, float scale) {
	return (point_t){
		.x = scale*p.x,
		.y = scale*p.y,
	};
}

bool eq_pt(const point_t p, const point_t q) {
	return (p.x == q.x) && (p.y == q.y);
}

point_t screen_to_pt(const int x, const int y) {
	const params_t par = state.params;
	return (point_t){
		.x = ((float) x - par.resolution.x / 2) / par.zoom - par.cam.x,
		.y = ((float) y - par.resolution.y / 2) / par.zoom - par.cam.y,
	};
}

/// Store the old parameters in state.old
void remember_old_params() {
	state.old.p = state.play_pt;
	state.old.delta = state.params.delta;
	state.old.epsilon = state.params.epsilon;
	state.old.radius = state.radius;
	state.old.volume = state.volume * state.start_volume;
}

/// Set the current playing point
void set_point(point_t p) {
	remember_old_params();
	if (state.params.view) {
		point_t floored = floor_pt(p);
		if (!eq_pt(state.params.p, floored)) {
			state.params.p = floored;
			state.play_pt = floored;
		}
	} else {
		state.params.delta = p.x;
		state.params.epsilon = p.y;
		state.play_pt = state.params.p;
	}
	state.volume = 1.0;
	state.params_changed = true;
}

/// Change the current playing point
void move_point(point_t q) {
	remember_old_params();
	if (state.params.view) {
		point_t floored = floor_pt(q);
		if (!eq_pt(state.params.p, floored)) {
			state.params.p = floored;
			state.play_pt = floored;
			state.volume = 1.0;
		}
	} else {
		state.params.delta = q.x;
		state.params.epsilon = q.y;
		state.smooth_change = true;
	}
	state.params_changed = true;
}

void input(const sapp_event* ev) {
	switch (ev->type) {
		case SAPP_EVENTTYPE_MOUSE_MOVE: {
			const point_t q = screen_to_pt(ev->mouse_x, ev->mouse_y);
			state.pointer = q;
			if (ev->modifiers & (SAPP_MODIFIER_MMB | SAPP_MODIFIER_SHIFT)) {
				state.params.cam.x += ev->mouse_dx / state.params.zoom;
				state.params.cam.y += ev->mouse_dy / state.params.zoom;
			} else if (ev->modifiers & SAPP_MODIFIER_LMB) {
				move_point(q);
			}
			break;
		}
		case SAPP_EVENTTYPE_MOUSE_DOWN: {
			const point_t p = screen_to_pt(ev->mouse_x, ev->mouse_y);
			switch (ev->mouse_button) {
				case SAPP_MOUSEBUTTON_LEFT:
    					set_point(p);
					break;
				case SAPP_MOUSEBUTTON_RIGHT: {
					const float tmp = state.params.zoom;
					state.params.zoom = state.other_zoom;
					state.other_zoom = tmp;
					if (state.params.view) {
						state.params.p = floor_pt(p);
						state.params.cam.x = state.params.delta;
						state.params.cam.y = state.params.epsilon;
					} else {
						state.params.delta = p.x;
						state.params.epsilon = p.y;
						state.params.cam = state.params.p;
						point_t zpt = (point_t){ 0, 0 };
						if (eq_pt(state.play_pt, zpt)) {
							state.params.p = zpt;
						}
					}
					state.params.view = !state.params.view;
					state.params.cam = screen_to_pt(ev->mouse_x,
					                                ev->mouse_y);
					break;
				}
				default:
					break;
			}
			break;
		}
		case SAPP_EVENTTYPE_MOUSE_SCROLL: {
			const point_t old = screen_to_pt(ev->mouse_x, ev->mouse_y);
			state.params.zoom *= pow(1.1, ev->scroll_y);
			const point_t new = screen_to_pt(ev->mouse_x, ev->mouse_y);
			state.params.cam.x += new.x - old.x;
			state.params.cam.y += new.y - old.y;
			break;
		case SAPP_EVENTTYPE_TOUCHES_BEGAN:
    			if (ev->num_touches == 1) {
				const point_t p = screen_to_pt(ev->touches[0].pos_x,
								ev->touches[0].pos_y);
				set_point(p);
    			}
    			break;
		case SAPP_EVENTTYPE_TOUCHES_MOVED:
    			if (ev->num_touches == 1) {
				const point_t q = screen_to_pt(ev->touches[0].pos_x,
								ev->touches[0].pos_y);
			    	move_point(q);
    			}
    			break;
		case SAPP_EVENTTYPE_KEY_DOWN:
			switch (ev->key_code) {
				case SAPP_KEYCODE_C:
					state.params.color = !state.params.color;
					break;
				case SAPP_KEYCODE_D:
					state.dampen = !state.dampen;
					break;
				case SAPP_KEYCODE_H:
					state.show_help = !state.show_help;
					break;
				case SAPP_KEYCODE_I:
					state.show_info = !state.show_info;
					break;
				case SAPP_KEYCODE_M:
					state.move = !state.move;
					break;
				case SAPP_KEYCODE_R:
					state.params.cam = (point_t){ 0.0, 0.0 };
					state.params.zoom = state.params.view ? 1.0 : 5000.0;
					break;
				case SAPP_KEYCODE_SPACE:
					state.play_pt = (point_t){ 0, 0 };
					if (state.params.view) {
						state.params.p = (point_t){ 0, 0 };
					}
					state.orbit_len = 0;
					break;
				case SAPP_KEYCODE_W:
				case SAPP_KEYCODE_COMMA:
					state.octave *= 2.0;
					break;
				case SAPP_KEYCODE_E:
				case SAPP_KEYCODE_PERIOD:
					state.octave /= 2.0;
					break;
				case SAPP_KEYCODE_0 ... SAPP_KEYCODE_9:
					if (ev->key_repeat) break;
					size_t code = ev->key_code - SAPP_KEYCODE_0;
					code = (code + 9) % 10;
					/// The period that plays the desired note
					const float period = 6.0 * state.octave / NOTES[code];
					const float pertrubation = ((float) rand() / (float) RAND_MAX - 0.5) * 0.01;
					state.params.delta = (float) (rand() % 12 + 1) / (float) (rand() % 16 + 4) + 0.5;
					state.params.epsilon = other_parameter(period + pertrubation, state.params.delta);
					state.dampen = false;
					state.volume = 1.0;
					if (state.params.view) {
    						const int x = ev->mouse_x, y = ev->mouse_y;
						state.params.p = floor_pt(screen_to_pt(x, y));
						state.play_pt = state.params.p;
					}
					state.params_changed = true;
					break;
				default:
					break;
			}
			break;
		    }
		case SAPP_EVENTTYPE_KEY_UP:
			switch(ev->key_code) {
				case SAPP_KEYCODE_0 ... SAPP_KEYCODE_9:
					state.dampen = true;
					break;
				default:
					break;
			}
			break;
		default:
			break;
	}
}

static void print_info() {
	const float period = calculate_period(state.params.delta, state.params.epsilon);
	if (state.params.view) {
		sdtx_printf("x: %d\n", (int) floor(state.pointer.x));
		sdtx_printf("y: %d\n", (int) floor(state.pointer.y));
		sdtx_printf("d: %f\n", state.params.delta);
		sdtx_printf("e: %f\n", state.params.epsilon);
		sdtx_printf("period: %f\n", period);
	} else {
		sdtx_printf("x: %d\n", (int) state.params.p.x);
		sdtx_printf("y: %d\n", (int) state.params.p.y);
		sdtx_printf("d: %f\n", state.pointer.x);
		sdtx_printf("e: %f\n", state.pointer.y);
		sdtx_printf("period: %f\n", calculate_period(state.pointer.x,
							     state.pointer.y));
	}
	if (state.orbit_len == MAX_ITERS) {
		sdtx_puts("orbit: too long to compute\n\n");
	} else {
		sdtx_printf("orbit: %ld\n\n", state.orbit_len);
	}

	// Print the spectrum
	for (size_t i = 1; i < state.orbit_len; i++) {
		if (cabsf(state.spectrum[i]) > 0.05) {
			const float cycle = (float) state.orbit_len/((float) i);
			sdtx_printf("%.3f = p/%.3f: %.3f (%.3fHz)\n", cycle, period/cycle,
				    cabsf(state.spectrum[i]), MAX_FREQ / cycle);
		}
	}
	sdtx_draw();
}

static void prepare_dark_rectangle() {
	sgl_load_pipeline(state.gfx.sgl_alpha_pip);
	sgl_c4f(0, 0, 0, 0.3);
	sgl_matrix_mode_projection();
	sgl_load_identity();
	sgl_ortho(0.0, 1.0, 1, 0, -1.0, 1.0);
	sgl_begin_quads();
	sgl_v2f(0.0, 0.0); sgl_v2f(1.0, 0.0);
	sgl_v2f(1.0, 1.0); sgl_v2f(0.0, 1.0);
	sgl_end();
}

static void frame() {
	// Change the parameter if move is enabled
	update_parameter(&state.params.epsilon, &state.params.delta, state.move*0.00002);
    
	const float w = sapp_widthf(), h = sapp_heightf();
	state.params.resolution = (point_t){ .x = w, .y = h };
	sg_begin_default_pass(&state.gfx.pass_action, (int) w, (int) h);
	sg_apply_pipeline(state.gfx.pip);
	sg_apply_bindings(&state.gfx.bind);
	sg_apply_uniforms(SG_SHADERSTAGE_FS, 0, &SG_RANGE(state.params));
	sg_draw(0, 3, 1);

	// Calculate the points in the new orbit
	if (state.params_changed) {
		state.params_changed = false;
		point_t p;
		if (state.smooth_change) {
    			p = state.play_pt;
			state.smooth_change = false;
		} else {
    			p = state.params.p;
		}
		point_t orig = state.orbit[0] = p;
		float r = p.x*p.x + p.y*p.y;
		state.radius = r > 0 ? r : 1e-12;
		for (state.orbit_len = 1; state.orbit_len < MAX_ITERS; state.orbit_len++) {
			p = ic_iter(p, state.params.delta, state.params.epsilon);
			if (eq_pt(p, orig)) break;
			state.orbit[state.orbit_len] = p;
			r = p.x*p.x + p.y*p.y;
			if (r > state.radius) {
				state.radius = r;
			}
		}

		// Calculate the spectrum in the orbit using FFT
		state.radius = sqrt(state.radius);
		const float scale = 1.0/state.radius;
		for (size_t i = 0; i < state.orbit_len; i++) {
			point_t q = state.orbit[i];
			state.spectrum[i] = scale*(q.x + I*q.y);
		}
		fft_transform(state.spectrum, state.orbit_len, false);
		for (size_t i = 0; i < state.orbit_len; i++) {
    			state.spectrum[i] /= (float) state.orbit_len;
		}

		state.start_volume = 0.4;
	}

	// Draw the orbit
	if (state.params.view) {
		sgl_layer(0);
		sgl_c3f(1.0, 0.0, 0.0);
		sgl_matrix_mode_projection();
		sgl_load_identity();
		sgl_ortho(-w/2.0, w/2.0, h/2.0, -h/2.0, -1.0, 1.0);
		sgl_scale(state.params.zoom, state.params.zoom, 1.0);
		sgl_translate(state.params.cam.x, state.params.cam.y, 0.0);
		sgl_begin_line_strip();

		for (size_t i = 0; i < state.orbit_len; i++) {
			point_t q = state.orbit[i];
			sgl_v2f(q.x + 0.5, q.y + 0.5);
		}
		sgl_v2f(state.orbit[0].x + 0.5, state.orbit[0].y + 0.5);
		sgl_end();
	}
	
	sgl_layer(1);
	prepare_dark_rectangle();
	sgl_draw_layer(0);
	
	sdtx_canvas(sapp_width()/2.0f, sapp_height()/2.0f);
	sdtx_color3b(255, 255, 255);
	sdtx_origin(3.0, 3.0);
	sdtx_home();
	if (state.show_help) {
		sgl_draw_layer(1);
		sdtx_puts(HELP);
		sdtx_draw();
	} else if (state.show_info) {
		sgl_draw_layer(1);
		print_info();
	}
	
	sg_end_pass();
	sg_commit();

	// Generate the audio samples
	size_t nsamples = saudio_expect();
	const size_t steps = saudio_sample_rate() / (float) MAX_FREQ;
	nsamples = steps * (nsamples / steps);

	const float scale = 1.0/state.radius;
	point_t p = scale_pt(state.play_pt, scale), prev = p;
	const float old_scale = 1.0/state.radius;
	point_t op = scale_pt(state.old.p, old_scale), oprev = op;
	for (size_t i = 0; i < nsamples; i++) {
		if (i % steps == 0) {
			prev = p;
			state.play_pt = ic_iter(state.play_pt, state.params.delta,
						state.params.epsilon);
			p = scale_pt(state.play_pt, scale);
			
			oprev = op;
			state.old.p = ic_iter(state.old.p, state.params.delta,
					      state.params.epsilon);
			op = scale_pt(state.old.p, old_scale);
		}

		if (state.dampen) {
			state.volume *= 0.99995;
		}
		if (state.start_volume < 1.0) {
    			state.start_volume *= 1.02;
		}
		state.old.volume *= 0.999;
		const float v = state.volume * state.start_volume;
		const float ov = state.old.volume;
		
		// Cosine interpolation
		float t = (float) (i % steps) / (float) steps;
		t = 0.5 - 0.5*cos(M_PI*t);
		state.audio_buffer[2*i] = v*((1 - t)*prev.x + t*p.x);
		state.audio_buffer[2*i] += ov*((1 - t)*oprev.x + t*op.x);
		state.audio_buffer[2*i + 1] = v*((1 - t)*prev.y + t*p.y);
		state.audio_buffer[2*i + 1] += ov*((1 - t)*oprev.y + t*op.y);
	}
	if (nsamples > 0) {
		saudio_push(state.audio_buffer, nsamples);
	}
}

static char* read_to_string(char* filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "Error: can't read %s", filename);
	}
	fseek(f, 0, SEEK_END);
	const size_t len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buffer = malloc(len + 1);
	fread(buffer, 1, len, f);
	buffer[len] = 0;
	fclose(f);
	return buffer;
}

static void init() {
	const char *fs_src = read_to_string("frag.glsl");

	sg_setup(&(sg_desc){
		.context = sapp_sgcontext(),
		.logger.func = slog_func,
	});
	sgl_setup(&(sgl_desc_t){
		.logger.func = slog_func,
	});
	sdtx_setup(&(sdtx_desc_t){
		.fonts = { [0] = sdtx_font_kc854() },
		.logger.func = slog_func
	});

	state.gfx.pass_action = (sg_pass_action) {
		.colors[0] = { .load_action = SG_LOADACTION_DONTCARE }
	};

	// Fullscreen triangle
	const float verts[] = { -1.0, -3.0, 3.0, 1.0, -1.0, 1.0 };
	state.gfx.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
		.data = SG_RANGE(verts)
	});
	state.gfx.pip = sg_make_pipeline(&(sg_pipeline_desc){
		.shader = sg_make_shader(&(sg_shader_desc){
			.attrs[0] = { .name="pos", .sem_name="POSITION" },
			.vs.source = VERTEX_SHADER,
			.fs.source = fs_src,
			.fs.uniform_blocks[0].size = sizeof(params_t),
			.fs.uniform_blocks[0].uniforms = {
				[0] = { .name = "iRes", .type = SG_UNIFORMTYPE_FLOAT2 },
				[1] = { .name = "iCam", .type = SG_UNIFORMTYPE_FLOAT2 },
				[2] = { .name = "iZoom", .type = SG_UNIFORMTYPE_FLOAT },
				[3] = { .name = "iDelta", .type = SG_UNIFORMTYPE_FLOAT },
				[4] = { .name = "iEpsilon", .type = SG_UNIFORMTYPE_FLOAT },
				[5] = { .name = "iPoint", .type = SG_UNIFORMTYPE_FLOAT2 },
				[6] = { .name = "iView", .type = SG_UNIFORMTYPE_INT },
				[7] = { .name = "iColor", .type = SG_UNIFORMTYPE_INT },
			},
		}),
		.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2,
		.colors[0].blend = {
			.enabled = true,
			.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
			.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		},
	});

	// A sokol-gl pipeline with alpha blending enabled
	state.gfx.sgl_alpha_pip = sgl_make_pipeline(&(sg_pipeline_desc){
		.depth.write_enabled = false,
		.colors[0].blend = {
			.enabled = true,
			.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
			.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		}
	});

	saudio_setup(&(saudio_desc){
	    .sample_rate = 48000,
	    .num_channels = 2,
	    .buffer_frames = 1024,
	    .logger.func = slog_func,
	});
}    

static void cleanup() {
	sdtx_shutdown();
	sgl_shutdown();
	sg_shutdown();
	saudio_shutdown();
}

sapp_desc sokol_main(int _argc, char* _argv[]) {
	return (sapp_desc) {
		.init_cb = init,
		.frame_cb = frame,
		.cleanup_cb = cleanup,
		.event_cb = input,
		.window_title = "Integer Circle Explorer",
		.logger.func = slog_func,
	};
}
