#include "tiny3d.h"

double accTime = 0.0;
#define TICK_RATE 20.0
#define SEC_PER_TICK (1.0 / TICK_RATE)

GLuint spriteSheet;
void draw_quad(int srcx, int srcy, int dstx, int dsty){
	float tx = (float)srcx / 16.0f, ty = (float)srcy / 16.0f;
	float x = (float)dstx, y = (float)dsty;
	glTexCoord2f(tx,ty); glVertex2f(x,y);
	glTexCoord2f(tx+1.0f/16.0f,ty); glVertex2f(x+1,y);
	glTexCoord2f(tx+1.0f/16.0f,ty+1.0f/16.0f); glVertex2f(x+1,y+1);
	glTexCoord2f(0,ty+1.0f/16.0f); glVertex2f(x,y+1);
}

char *curLevel;

typedef struct {
	vec2 dim,prevPos,curPos,vel;
	vec3 color;
	bool onGround;
} Entity;

vec2 camPrevPos,camCurPos;

typedef struct {
	vec2 min,max;
} MMBB;

void get_entity_mmbb(Entity *e, MMBB *m){
	for (int i = 0; i < 2; i++){
		m->min[i] = e->curPos[i]-0.5f*e->dim[i];
		m->max[i] = e->curPos[i]+0.5f*e->dim[i];
	}
}

void get_expanded_mmbb(MMBB *src, MMBB *dst, vec2 v){
	*dst = *src;
	for (int i = 0; i < 2; i++){
		if (v[i] > 0){
			dst->max[i] += v[i];
		} else {
			dst->min[i] += v[i];
		}
	}
}

void get_mmbb_center(MMBB *m, vec2 c){
	for (int i = 0; i < 2; i++){
		c[i] = m->min[i] + 0.5f*(m->max[i]-m->min[i]);
	}
}

void get_entity_interp_pos(Entity *e, vec2 pos){
	vec2_lerp(e->prevPos,e->curPos,accTime/SEC_PER_TICK,pos);
}

void draw_entity(Entity *e){
	vec2 pos;
	get_entity_interp_pos(e,pos);
	glColor3f(e->color[0],e->color[1],e->color[2]);
	glBegin(GL_QUADS);
	glVertex2f(pos[0]-0.5f*e->dim[0],pos[1]-0.5f*e->dim[1]);
	glVertex2f(pos[0]+0.5f*e->dim[0],pos[1]-0.5f*e->dim[1]);
	glVertex2f(pos[0]+0.5f*e->dim[0],pos[1]+0.5f*e->dim[1]);
	glVertex2f(pos[0]-0.5f*e->dim[0],pos[1]+0.5f*e->dim[1]);
	glEnd();
}

void update_entity(Entity *e){
	vec2_copy(e->curPos,e->prevPos);
	e->vel[1] -= 0.075f; //gravity
	vec2 d;
	vec2_copy(e->vel,d);

	MMBB m,em;
	get_entity_mmbb(e,&m);
	get_expanded_mmbb(&m,&em,d);

	for (int tx = (int)em.min[0]; tx <= (int)em.max[0]; tx++){
		for (int ty = (int)em.min[1]; ty <= (int)em.max[1]; ty++){
			if (curLevel[(16-ty)*16+tx]==1 &&
			m.min[0] < (tx+1) && m.max[0] > tx){
				if (d[1] < 0 && m.min[1] >= (ty+1)){
					float nd = (ty+1) - m.min[1];
					if (nd > d[1]){
						d[1] = nd + 0.001f;
					}
				} else if (d[1] > 0 && m.max[1] <= ty){
					float nd = ty - m.max[1];
					if (nd < d[1]){
						d[1] = nd - 0.001f;
					}
				}
			}
		}
	}
	m.min[1] += d[1];
	m.max[1] += d[1];
	for (int tx = (int)em.min[0]; tx <= (int)em.max[0]; tx++){
		for (int ty = (int)em.min[1]; ty <= (int)em.max[1]; ty++){
			if (curLevel[(16-ty)*16+tx]==1 &&
			m.min[1] < (ty+1) && m.max[1] > ty){
				if (d[0] < 0 && m.min[0] >= (tx+1)){
					float nd = (tx+1) - m.min[0];
					if (nd > d[0]){
						d[0] = nd + 0.001f;
					}
				} else if (d[0] > 0 && m.max[0] <= tx){
					float nd = tx - m.max[0];
					if (nd < d[0]){
						d[0] = nd - 0.001f;
					}
				}
			}
		}
	}
	m.min[0] += d[0];
	m.max[0] += d[0];

	get_mmbb_center(&m,e->curPos);

	if (d[0] != e->vel[0]){
		e->vel[0] = 0.0f;
	}
	if (d[1] != e->vel[1]){
		if (e->vel[1] < 0.0f){
			e->onGround = true;
		}
		e->vel[1] = 0.0f;
	} else {
		e->onGround = false;
	}
}

Entity player = {
	.dim = {0.6f,1.2f},
	.color = {1.0f,1.0f,1.0f}
};

char level1[16*16] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,
	0,0,0,0,0,2,0,0,1,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,
	0,0,0,0,1,1,1,0,0,0,0,0,1,0,0,0,
	0,0,0,0,1,0,0,1,0,0,0,1,1,0,0,0,
	0,0,0,0,1,0,0,1,0,0,1,1,1,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

void load_level(char *level){
	curLevel = level;
	for (int y = 0; y < 16; y++){
		for (int x = 0; x < 16; x++){
			if (level[(16-y)*16+x] == 2){
				player.curPos[0] = x+0.5f;
				player.curPos[1] = y+0.5f;
				vec2_copy(player.curPos,player.prevPos);
			}
		}
	}
}

struct {
	bool
	left,
	right,
	jump;
} keys;

void keydown(int key){
	if (key == 'F'){
		exit(0);
	} else if (key == 'C'){
		lock_mouse(!is_mouse_locked());
	}
	switch (key){
		case 'D': keys.right = true; break;
		case 'A': keys.left = true; break;
		case ' ': if (player.onGround) player.vel[1] = 0.5f; break;
	}
}

void keyup(int key){
	switch (key){
		case 'D': keys.right = false; break;
		case 'A': keys.left = false; break;
	}
}

void mousemove(int x, int y){
}

void update(double time, double deltaTime, int width, int height, int nAudioFrames, int16_t *audioSamples){
	static bool init = false;
	if (!init){
		init = true;

		int w,h;
		uint32_t *p = load_image(true,&w,&h,"textures/sprites.png");
		glGenTextures(1,&spriteSheet);
		glBindTexture(GL_TEXTURE_2D,spriteSheet);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,p);
	}

	accTime += deltaTime;
	while (accTime >= 1.0/20.0){
		accTime -= 1.0/20.0;
		
		//TICK:
		if ((keys.left || keys.right) && !(keys.left && keys.right)){
			player.vel[0] = LERP(player.vel[0],keys.left ? -0.2f : 0.2f,0.2f);
		} else {
			player.vel[0] = LERP(player.vel[0],0,0.2f);
		}
		update_entity(&player);
		vec2_copy(camCurPos,camPrevPos);
		vec2_lerp(camCurPos,player.curPos,0.2f,camCurPos);
	}

	//DRAW:	
	glViewport(0,0,width,height);

	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(90.0,(double)width/height,0.01,1000.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	vec2 pos;
	vec2_lerp(camPrevPos,camCurPos,accTime/SEC_PER_TICK,pos);
	glTranslatef(-pos[0],-pos[1],-5);

	draw_entity(&player);

	glEnable(GL_TEXTURE_2D);
	glColor3f(1,1,1);
	glBegin(GL_QUADS);
	for (int y = 0; y < 16; y++){
		for (int x = 0; x < 16; x++){
			if (level1[(16-y)*16+x]==1){
				draw_quad(0,11,x,y);
			}
		}
	}
	glEnd();
	glDisable(GL_TEXTURE_2D);
}

int main(int argc, char **argv){
	load_level(level1);
    open_window(640,480);
}