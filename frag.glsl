#version 100

#define ITERS 512

uniform mediump vec2 iRes;
uniform mediump vec2 iCam;
uniform mediump float iZoom;
uniform mediump float iDelta;
uniform mediump float iEpsilon;
uniform mediump vec2 iPoint;
uniform int iView;
uniform int iColor;

mediump vec3 fractal(mediump vec2 z, mediump float delta, mediump float epsilon) {
	if (iView == 1 && z == iPoint) {
		return vec3(1.0, 0.0, 0.0);
	}

	mediump vec2 pz = z;
	int i;
	for (int j = 0; j < ITERS; ++j) {
		i = j;
		z.x -= floor(delta * z.y);
		z.y += floor(epsilon * z.x);
		z.x -= floor(delta * z.y);
		if (z == pz) { break; }
		if (iView == 1 && z == iPoint) {
			return vec3(1.0, 0.0, 0.0);
		}
	}

    if (iColor == 1) {
        // Colors in the YCoCg space
	    lowp float y = (1.0 - float(i)/float(ITERS));
	    y = pow(y, 1.5);
	    lowp float co = sin(float(i) * 0.1) * min(y, 1.0-y)/sqrt(2.0);
	    lowp float cg = cos(float(i) * 0.1) * min(y, 1.0-y)/sqrt(2.0);

	    lowp float tmp = y - cg;
	    return vec3(tmp + co, y + cg, tmp - co);
    } else {
        lowp float r = sin(float(i) * 0.1) * 0.5 + 0.5;
	    lowp float g = cos(float(i) * 0.1) * 0.5 + 0.5;
	    lowp float l = log(float(i))/log(float(ITERS));
	    lowp float b = 1.0 - 2.0*l + 2.0*l*l;

	    lowp float scale = 1.0 - float(i)/float(ITERS);
	    return scale*vec3(r, g, b);
    }
}

void main() {
	mediump vec2 screen_pos = gl_FragCoord.xy - (iRes.xy * 0.5);

	mediump vec2 c = vec2((screen_pos * vec2(1.0, -1.0)) / iZoom - iCam);
	lowp vec3 col;
	if (iView == 1) {
		mediump float a = 2.0*iEpsilon/sqrt(iDelta*iEpsilon*(4.0 - iDelta*iEpsilon));
		mediump float b = sqrt(iDelta*iEpsilon/(4.0 - iDelta*iEpsilon));
		col = fractal(floor(c), iDelta, iEpsilon);
	} else {
		col = fractal(iPoint, c.x, c.y);
	}

	gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
