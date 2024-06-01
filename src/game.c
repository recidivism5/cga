#include "tiny3d.h"

double accumulated_time = 0.0;
double interpolant;
#define TICK_RATE 20.0
#define SEC_PER_TICK (1.0 / TICK_RATE)

typedef struct {
	uint8_t r,g,b,a;
} color_t;

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 100
color_t screen[SCREEN_WIDTH*SCREEN_HEIGHT];

color_t cga_colors[] = {
	{0x00,0x00,0x00,0xFF},
	{0x00,0x00,0xAA,0xFF},
	{0x00,0xAA,0x00,0xFF},
	{0x00,0xAA,0xAA,0xFF},
	{0xAA,0x00,0x00,0xFF},
	{0xAA,0x00,0xAA,0xFF},
	{0xAA,0x55,0x00,0xFF},
	{0xAA,0xAA,0xAA,0xFF},
	{0x55,0x55,0x55,0xFF},
	{0x55,0x55,0xFF,0xFF},
	{0x55,0xFF,0x55,0xFF},
	{0x55,0xFF,0xFF,0xFF},
	{0xFF,0x55,0x55,0xFF},
	{0xFF,0x55,0xFF,0xFF},
	{0xFF,0xFF,0x55,0xFF},
	{0xFF,0xFF,0xFF,0xFF},
};

typedef struct {
	vec3 min,max;
} mmbb_t;

typedef struct {
	ivec3 min,max;
} immbb_t;

#define WORLD_WIDTH 32
uint8_t world[WORLD_WIDTH*WORLD_WIDTH*WORLD_WIDTH];

uint8_t *get_block(int x, int y, int z){
	if (x >= 0 && x < WORLD_WIDTH &&
		y >= 0 && y < WORLD_WIDTH &&
		z >= 0 && z < WORLD_WIDTH){
		return world + y*WORLD_WIDTH*WORLD_WIDTH + z*WORLD_WIDTH + x;
	} else {
		return 0;
	}
}

typedef struct {
	bool on_ground;
	float width, height;
	vec3 previous_position;
	vec3 current_position;
	vec3 velocity;
	vec2 head_rotation;
} entity_t;

entity_t player = {
	.width = 0.6f,
	.height = 1.8f,
};

void entity_set_position(entity_t *e, float x, float y, float z){
	e->current_position[0] = x;
	e->current_position[1] = y;
	e->current_position[2] = z;
	e->previous_position[0] = x;
	e->previous_position[1] = y;
	e->previous_position[2] = z;
}

void get_entity_interpolated_position(entity_t *e, vec3 position){
	vec3_lerp(e->previous_position,e->current_position,(float)interpolant,position);
}

void get_entity_mmbb(entity_t *e, mmbb_t *m){
	m->min[0] = e->current_position[0]-0.5f*e->width;
	m->min[1] = e->current_position[1]-0.5f*e->height;
	m->min[2] = e->current_position[2]-0.5f*e->width;
	m->max[0] = e->current_position[0]+0.5f*e->width;
	m->max[1] = e->current_position[1]+0.5f*e->height;
	m->max[2] = e->current_position[2]+0.5f*e->width;
}

void get_expanded_mmbb(mmbb_t *src, mmbb_t *dst, vec3 v){
	*dst = *src;
	for (int i = 0; i < 3; i++){
		if (v[i] > 0){
			dst->max[i] += v[i];
		} else {
			dst->min[i] += v[i];
		}
	}
}

void get_mmbb_center(mmbb_t *m, vec2 c){
	for (int i = 0; i < 3; i++){
		c[i] = m->min[i] + 0.5f*(m->max[i]-m->min[i]);
	}
}

void update_entity(entity_t *e){
	vec3_copy(e->current_position,e->previous_position);
	e->velocity[1] -= 0.075f; //gravity
	vec3 d;
	vec3_copy(e->velocity,d);

	mmbb_t m,em;
	get_entity_mmbb(e,&m);
	get_expanded_mmbb(&m,&em,d);
	immbb_t im;
	for (int i = 0; i < 3; i++){
		im.min[i] = (int)floorf(em.min[i]);
		im.max[i] = (int)floorf(em.max[i]);
	}

	/*for (int y = im.min[1]; y <= im.max[1]; y++){
		for (int z = im.min[2]; z <= im.max[2]; z++){
			for (int x = im.min[0]; x <= im.max[0]; x++){
				block_t *b = get_block(x,y,z);
				if (b && b->id &&
					m.min[0] < (x+1) && m.max[0] > x &&
					m.min[2] < (z+1) && m.max[2] > z){
					if (d[1] < 0 && m.min[1] >= (y+1)){
						float nd = (y+1) - m.min[1];
						if (nd > d[1]){
							d[1] = nd + 0.001f;
						}
					} else if (d[1] > 0 && m.max[1] <= y){
						float nd = y - m.max[1];
						if (nd < d[1]){
							d[1] = nd - 0.001f;
						}
					}
				}
			}
		}
	}
	m.min[1] += d[1];
	m.max[1] += d[1];

	for (int y = im.min[1]; y <= im.max[1]; y++){
		for (int z = im.min[2]; z <= im.max[2]; z++){
			for (int x = im.min[0]; x <= im.max[0]; x++){
				block_t *b = get_block(x,y,z);
				if (b && b->id &&
					m.min[1] < (y+1) && m.max[1] > y &&
					m.min[2] < (z+1) && m.max[2] > z){
					if (d[0] < 0 && m.min[0] >= (x+1)){
						float nd = (x+1) - m.min[0];
						if (nd > d[0]){
							d[0] = nd + 0.001f;
						}
					} else if (d[0] > 0 && m.max[0] <= x){
						float nd = x - m.max[0];
						if (nd < d[0]){
							d[0] = nd - 0.001f;
						}
					}
				}
			}
		}
	}
	m.min[0] += d[0];
	m.max[0] += d[0];

	for (int y = im.min[1]; y <= im.max[1]; y++){
		for (int z = im.min[2]; z <= im.max[2]; z++){
			for (int x = im.min[0]; x <= im.max[0]; x++){
				block_t *b = get_block(x,y,z);
				if (b && b->id &&
					m.min[1] < (y+1) && m.max[1] > y &&
					m.min[0] < (x+1) && m.max[0] > x){
					if (d[2] < 0 && m.min[2] >= (z+1)){
						float nd = (z+1) - m.min[2];
						if (nd > d[2]){
							d[2] = nd + 0.001f;
						}
					} else if (d[2] > 0 && m.max[2] <= z){
						float nd = z - m.max[2];
						if (nd < d[2]){
							d[2] = nd - 0.001f;
						}
					}
				}
			}
		}
	}
	m.min[2] += d[2];
	m.max[2] += d[2];*/

	get_mmbb_center(&m,e->current_position);

	if (d[0] != e->velocity[0]){
		e->velocity[0] = 0.0f;
	}
	if (d[2] != e->velocity[2]){
		e->velocity[2] = 0.0f;
	}
	if (d[1] != e->velocity[1]){
		if (e->velocity[1] < 0.0f){
			e->on_ground = true;
		}
		e->velocity[1] = 0.0f;
	} else {
		e->on_ground = false;
	}
}

/*typedef struct {
	block_t *block;
	ivec3 block_pos;
	ivec3 face_normal;
	float t;
} block_raycast_result_t;

void cast_ray_into_blocks(vec3 origin, vec3 ray, block_raycast_result_t *result){
	result->block_pos[0] = (int)floorf(origin[0]);
	result->block_pos[1] = (int)floorf(origin[1]);
	result->block_pos[2] = (int)floorf(origin[2]);
	vec3 da;
	for (int i = 0; i < 3; i++){
		if (ray[i] < 0){
			da[i] = ((float)result->block_pos[i]-origin[i]) / ray[i];
		} else {
			da[i] = ((float)result->block_pos[i]+1.0f-origin[i]) / ray[i];	
		}
	}
	result->t = 0;
	int index = 0;
	while (result->t <= 1.0f){
		result->block = get_block(result->block_pos[0],result->block_pos[1],result->block_pos[2]);
		if (result->block && result->block->id){
			for (int i = 0; i < 3; i++){
				result->face_normal[i] = 0;
			}
			result->face_normal[index] = ray[index] < 0 ? 1 : -1;
			return;
		}
		float d = HUGE_VALF;
		index = 0;
		for (int i = 0; i < 3; i++){
			if (da[i] < d){
				index = i;
				d = da[i];
			}
		}
		result->block_pos[index] += ray[index] < 0 ? -1 : 1;
		result->t += da[index];
		for (int i = 0; i < 3; i++){
			da[i] -= d;
		}
		da[index] = fabsf(1.0f / ray[index]);
	}
	result->block = 0;
}*/

void get_player_head_pos(vec3 out){
	get_entity_interpolated_position(&player,out);
	out[1] += 1.62f-0.9f;
}

/*void get_player_target_block(block_raycast_result_t *result){
	vec3 head;
	get_player_head_pos(head);
	vec3 ray = {0,0,-5};
	vec3_rotate_deg(ray,(vec3){1,0,0},-player.head_rotation[0],ray);
	vec3_rotate_deg(ray,(vec3){0,1,0},-player.head_rotation[1],ray);
	cast_ray_into_blocks(head,ray,result);
}*/

float mouse_sensitivity = 0.1f;

struct {
	bool
		left,
		right,
		backward,
		forward,
		jump,
		crouch,
		attack,
		just_attacked,
		interact,
		just_interacted;
} keys;

void keydown(int key){
	static bool fog = false;
	switch (key){
		case 27: exit(0); break;
		case 'P': toggle_fullscreen(); break;
		case 'C': lock_mouse(!is_mouse_locked()); break;
		case 'F': fog ? glDisable(GL_FOG) : glEnable(GL_FOG); fog = !fog; break;
		case 'W': keys.forward = true; break;
		case 'A': keys.left = true; break;
		case 'S': keys.backward = true; break;
		case 'D': keys.right = true; break;
		case ' ': keys.jump = true; break;
		case KEY_MOUSE_LEFT:{
			break;
		}
		case KEY_MOUSE_RIGHT:{
			break;
		}
	}
}

void keyup(int key){
	switch (key){
		case 'W': keys.forward = false; break;
		case 'A': keys.left = false; break;
		case 'S': keys.backward = false; break;
		case 'D': keys.right = false; break;
		case ' ': keys.jump = false; break;
	}
}

void mousemove(int x, int y){
	if (is_mouse_locked()){
		player.head_rotation[0] += y*mouse_sensitivity;
		if (player.head_rotation[0] < -90.0){
			player.head_rotation[0] = -90.0;
		} else if (player.head_rotation[0] > 90.0){
			player.head_rotation[0] = 90.0;
		}
		player.head_rotation[1] += x*mouse_sensitivity;
		while (player.head_rotation[1] > 360.0){
			player.head_rotation[1] -= 360.0;
		}
		while (player.head_rotation[1] < 0){
			player.head_rotation[1] += 360.0;
		}
	}
}

extern void scroll(float deltaX, float deltaY){
	printf("%f %f\n",deltaX, deltaY);
}
extern void zoom(float zoomDelta){

}
float textAngle;
extern void rotate(float angleDelta){
	textAngle += angleDelta*0.5f;
}

void tick(){
	ivec2 move_dir;
	if (keys.left && keys.right){
		move_dir[0] = 0;
	} else if (keys.left){
		move_dir[0] = -1;
	} else if (keys.right){
		move_dir[0] = 1;
	} else {
		move_dir[0] = 0;
	}
	if (keys.backward && keys.forward){
		move_dir[1] = 0;
	} else if (keys.backward){
		move_dir[1] = 1;
	} else if (keys.forward){
		move_dir[1] = -1;
	} else {
		move_dir[1] = 0;
	}
	vec3 move_vec = {(float)move_dir[0],0,(float)move_dir[1]};
	if (move_dir[0] || move_dir[1]){
		vec3_normalize(move_vec,move_vec);
		vec3_scale(move_vec,0.25f,move_vec);
		vec3_rotate_deg(move_vec,(vec3){0,1,0},-player.head_rotation[1],move_vec);
	}
	player.velocity[0] = LERP(player.velocity[0],move_vec[0],0.3f);
	player.velocity[2] = LERP(player.velocity[2],move_vec[2],0.3f);
	if (player.on_ground && keys.jump){
		player.velocity[1] = 0.5f;
	}
	update_entity(&player);
}

#define TEXT_IMG_WIDTH 512
uint32_t textImg[TEXT_IMG_WIDTH*TEXT_IMG_WIDTH];
GLuint textImgTid;

void update(double time, double deltaTime, int width, int height, int nAudioFrames, int16_t *audioSamples){
	static bool init = false;
	if (!init){
		init = true;

		lock_mouse(true);

		entity_set_position(&player,8,8,8);
	}

	accumulated_time += deltaTime;
	while (accumulated_time >= 1.0/20.0){
		accumulated_time -= 1.0/20.0;
		tick();
	}
	interpolant = accumulated_time / SEC_PER_TICK;

	vec3 cam_pos;
	get_player_head_pos(cam_pos);

	//DRAW:	
	glViewport(0,0,width,height);

	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}

int main(int argc, char **argv){
	init_cga_colors();
	for (int i = 0; i < 16; i++){
		printf("%02x %02x %02x vs %06x\n",cga_colors[i].r,cga_colors[i].g,cga_colors[i].b,cga_colors_check[i]);
	}
    open_window(640,480);
}