#include "tiny3d.h"

double accumulated_time = 0.0;
double interpolant;
#define TICK_RATE 20.0
#define SEC_PER_TICK (1.0 / TICK_RATE)

GLuint block_texture_id;

typedef struct {
	char *name;
	bool transparent;
	int faces[6][2];
} block_type_t;

block_type_t block_types[] = {
	{"air",true,{{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}},
	{"bedrock",false,{{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}},
	{"stone",false,{{1,0},{1,0},{1,0},{1,0},{1,0},{1,0}}},
	{"dirt",false,{{0,3},{0,3},{0,3},{0,3},{0,3},{0,3}}},
	{"grass",false,{{0,2},{0,2},{0,3},{0,1},{0,2},{0,2}}},
	{"log",false,{{1,3},{1,3},{1,2},{1,2},{1,3},{1,3}}},
	{"glass",true,{{8,0},{8,0},{8,0},{8,0},{8,0},{8,0}}},
	{"red_glass",true,{{8,1},{8,1},{8,1},{8,1},{8,1},{8,1}}},
	{"green_glass",true,{{8,2},{8,2},{8,2},{8,2},{8,2},{8,2}}},
	{"blue_glass",true,{{8,3},{8,3},{8,3},{8,3},{8,3},{8,3}}},
	{"brick",false,{{10,0},{10,0},{10,0},{10,0},{10,0},{10,0}}},
};

typedef enum {
	BLOCK_AIR,
	BLOCK_BEDROCK,
	BLOCK_STONE,
	BLOCK_DIRT,
	BLOCK_GRASS,
	BLOCK_LOG,
	BLOCK_GLASS,
	BLOCK_RED_GLASS,
	BLOCK_GREEN_GLASS,
	BLOCK_BLUE_GLASS,
	BLOCK_BRICK,
} block_id_t;

typedef struct {
	uint8_t id, light;
} block_t;

typedef struct {
	float x,y,z,u,v;
	uint8_t r,g,b,a;
} vertex_t;

typedef struct {
	int total, used;
	vertex_t *elements;
} vertex_list_t;

vertex_t *vertex_list_make_room(vertex_list_t *list, int count){
	if (list->used+count > list->total){
		if (!list->total) list->total = 1;
		while (list->used+count > list->total) list->total *= 2;
		list->elements = realloc(list->elements,list->total*sizeof(*list->elements));
		ASSERT(list->elements);
	}
	list->used += count;
	return list->elements+list->used-count;
}

#define WORLD_WIDTH 32
#define WORLD_HEIGHT 64

block_t world[WORLD_WIDTH*WORLD_WIDTH*WORLD_HEIGHT];

vertex_list_t world_opaque_mesh, world_transparent_mesh;

void gen_world(){
	for (int y = 0; y < WORLD_HEIGHT; y++){
		for (int z = 0; z < WORLD_WIDTH; z++){
			for (int x = 0; x < WORLD_WIDTH; x++){
				block_t *b = world + y*WORLD_WIDTH*WORLD_WIDTH + z*WORLD_WIDTH + x;
				b->id = y==0 ? BLOCK_BEDROCK : y==63 ? rand()%2? BLOCK_GRASS : BLOCK_AIR : BLOCK_DIRT;
				b->light = 255;
			}
		}
	}
}

float ambient_light_coefficients[6] = {0.6f,0.6f,0.5f,1.0f,0.8f,0.8f};

float light_coefficients[16];

void init_light_coefficients(){
	for (int i = 0; i < COUNT(light_coefficients); i++){
		float a = i/15.0f;
		light_coefficients[i] = a / ((1-a) * 3 + 1);
	}
}

#define SKYLIGHT(v) ((v)>>4)
#define BLOCKLIGHT(v) ((v)&0x0f)

float cube_vertices[] = {
	0,1,0, 0,0,0, 0,0,1, 0,0,1, 0,1,1, 0,1,0,
	1,1,1, 1,0,1, 1,0,0, 1,0,0, 1,1,0, 1,1,1,

	0,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,0,
	1,1,0, 0,1,0, 0,1,1, 0,1,1, 1,1,1, 1,1,0,

	1,1,0, 1,0,0, 0,0,0, 0,0,0, 0,1,0, 1,1,0,
	0,1,1, 0,0,1, 1,0,1, 1,0,1, 1,1,1, 0,1,1,
};

void append_block_face(int x, int y, int z, int face_id, block_t *neighbor, block_type_t *type){
	vertex_t *v = vertex_list_make_room(type->transparent ? &world_transparent_mesh : &world_opaque_mesh, 6);
	uint8_t light = neighbor ? 
		(uint8_t) (255 * (ambient_light_coefficients[face_id] * light_coefficients[MAX(SKYLIGHT(neighbor->light),BLOCKLIGHT(neighbor->light))])) :
		255;
	for (int i = 0; i < 6; i++){
		v[i].x = x + cube_vertices[(face_id * 6 + i)*3 + 0];
		v[i].y = y + cube_vertices[(face_id * 6 + i)*3 + 1];
		v[i].z = z + cube_vertices[(face_id * 6 + i)*3 + 2];
		v[i].r = light;
		v[i].g = light;
		v[i].b = light;
		v[i].a = 255;
	}
	float w = 1.0f/16.0f;

	v[0].u = w*type->faces[face_id][0];
	v[0].v = w*(16-type->faces[face_id][1]);

	v[1].u = v[0].u;
	v[1].v = v[0].v - w;

	v[2].u = v[0].u + w;
	v[2].v = v[1].v;

	v[3].u = v[2].u;
	v[3].v = v[2].v;

	v[4].u = v[2].u;
	v[4].v = v[0].v;

	v[5].u = v[0].u;
	v[5].v = v[0].v;
}

void test_neighbor(int x, int y, int z, int face_id, int xx, int yy, int zz, block_type_t *type){
	if (xx > 0 && xx < (WORLD_WIDTH-1) &&
		yy > 0 && yy < (WORLD_HEIGHT-1) &&
		zz > 0 && zz < (WORLD_WIDTH-1)){
		block_t *nb = world + (y-1)*WORLD_WIDTH*WORLD_WIDTH + z*WORLD_WIDTH + x;
		block_type_t *nbt = block_types+nb->id;
		if (nbt->transparent){
			append_block_face(x,y,z,face_id,nb,type);
		}
	} else {
		append_block_face(x,y,z,face_id,0,type);
	}
}

void mesh_world(){
	world_opaque_mesh.used = 0;
	world_transparent_mesh.used = 0;
	for (int y = 0; y < WORLD_HEIGHT; y++){
		for (int z = 0; z < WORLD_WIDTH; z++){
			for (int x = 0; x < WORLD_WIDTH; x++){
				block_t *b = world + y*WORLD_WIDTH*WORLD_WIDTH + z*WORLD_WIDTH + x;
				block_type_t *bt = block_types+b->id;
				if (b->id){
					test_neighbor(x,y,z,0,x-1,y,z,bt);
					test_neighbor(x,y,z,1,x+1,y,z,bt);
					test_neighbor(x,y,z,2,x,y-1,z,bt);
					test_neighbor(x,y,z,3,x,y+1,z,bt);
					test_neighbor(x,y,z,4,x,y,z-1,bt);
					test_neighbor(x,y,z,5,x,y,z+1,bt);
				}
			}
		}
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

typedef struct {
	vec3 min,max;
} mmbb_t;

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

block_t *get_block(int x, int y, int z){
	if (x >= 0 && x < WORLD_WIDTH &&
		y >= 0 && y < WORLD_HEIGHT &&
		z >= 0 && z < WORLD_WIDTH){
		return world + y*WORLD_WIDTH*WORLD_WIDTH + z*WORLD_WIDTH + x;
	} else {
		return 0;
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

	for (int y = (int)em.min[1]; y <= (int)em.max[1]; y++){
		for (int z = (int)em.min[2]; z <= (int)em.max[2]; z++){
			for (int x = (int)em.min[0]; x <= (int)em.max[0]; x++){
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

	for (int y = (int)em.min[1]; y <= (int)em.max[1]; y++){
		for (int z = (int)em.min[2]; z <= (int)em.max[2]; z++){
			for (int x = (int)em.min[0]; x <= (int)em.max[0]; x++){
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

	for (int y = (int)em.min[1]; y <= (int)em.max[1]; y++){
		for (int z = (int)em.min[2]; z <= (int)em.max[2]; z++){
			for (int x = (int)em.min[0]; x <= (int)em.max[0]; x++){
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
	m.max[2] += d[2];

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

/*void move_aabb_against_chunks(AABB *a, double dt){
	vec3 d;
	glm_vec3_scale(a->velocity,dt,d);
	vec3 d_initial;
	glm_vec3_copy(d,d_initial);
	MMBB m;
	aabb_to_mmbb(a,&m);
	MMBB exp_m = m;
	expand_mmbb(&exp_m,d);
	ivec3 block_min, block_max;
	for (int i = 0; i < 3; i++){
		block_min[i] = floorf(exp_m.min[i]);
		block_max[i] = floorf(exp_m.max[i]);
	}
	MMBBList mbl = {0};
	for (int y = block_min[1]; y <= block_max[1]; y++){
		for (int z = block_min[2]; z <= block_max[2]; z++){
			for (int x = block_min[0]; x <= block_max[0]; x++){
				Block *b = get_block(chunks,(ivec3){x,y,z});
				if (b && b->id){
					MMBB *bm = MMBBListMakeRoom(&mbl,1);
					bm->min[0] = x;
					bm->min[1] = y;
					bm->min[2] = z;
					bm->max[0] = x+1;
					bm->max[1] = y+1;
					bm->max[2] = z+1;
				}
			}
		}
	}
	for (MMBB *bm = mbl.elements; bm < mbl.elements+mbl.used; bm++){
		if (m.min[0] < bm->max[0] && bm->min[0] < m.max[0] && 
			m.min[2] < bm->max[2] && bm->min[2] < m.max[2]){
			if (d[1] < 0 && bm->max[1] <= m.min[1]){
				float nd = bm->max[1] - m.min[1];
				if (d[1] < nd){
					d[1] = nd + 0.001f;
				}
			} else if (d[1] > 0 && m.max[1] <= bm->min[1]){
				float nd = bm->min[1] - m.max[1];
				if (nd < d[1]){
					d[1] = nd - 0.001f;
				}
			}
		}
	}
	m.min[1] += d[1];
	m.max[1] += d[1];
	for (MMBB *bm = mbl.elements; bm < mbl.elements+mbl.used; bm++){
		if (m.min[0] < bm->max[0] && bm->min[0] < m.max[0] &&
			m.min[1] < bm->max[1] && bm->min[1] < m.max[1]){
			if (d[2] < 0 && bm->max[2] <= m.min[2]){
				float nd = bm->max[2] - m.min[2];
				if (d[2] < nd){
					d[2] = nd + 0.001f;
				}
			} else if (d[2] > 0 && m.max[2] <= bm->min[2]){
				float nd = bm->min[2] - m.max[2];
				if (nd < d[2]){
					d[2] = nd - 0.001f;
				}
			}
		}
	}
	m.min[2] += d[2];
	m.max[2] += d[2];
	for (MMBB *bm = mbl.elements; bm < mbl.elements+mbl.used; bm++){
		if (m.min[1] < bm->max[1] && bm->min[1] < m.max[1] &&
			m.min[2] < bm->max[2] && bm->min[2] < m.max[2]){
			if (d[0] < 0 && bm->max[0] <= m.min[0]){
				float nd = bm->max[0] - m.min[0];
				if (d[0] < nd){
					d[0] = nd + 0.001f;
				}
			} else if (d[0] > 0 && m.max[0] <= bm->min[0]){
				float nd = bm->min[0] - m.max[0];
				if (nd < d[0]){
					d[0] = nd - 0.001f;
				}
			}
		}
	}
	m.min[0] += d[0];
	m.max[0] += d[0];
	if (mbl.used){
		free(mbl.elements);
	}
	mmbb_get_center(&m,a->position);
	if (d[0] != d_initial[0]){
		a->velocity[0] = 0.0f;
	}
	if (d[1] != d_initial[1]){
		a->velocity[1] = 0.0f;
		if (d_initial[1] < 0.0f){
			a->on_ground = true;
		}
	} else {
		a->on_ground = false;
	}
	if (d[2] != d_initial[2]){
		a->velocity[2] = 0.0f;
	}
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
	if (key == 'F'){
		exit(0);
	} else if (key == 'C'){
		lock_mouse(!is_mouse_locked());
	}
	switch (key){
		case 'W': keys.forward = true; break;
		case 'A': keys.left = true; break;
		case 'S': keys.backward = true; break;
		case 'D': keys.right = true; break;
		case ' ': keys.jump = true; break;
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
	vec3 move_vec = {move_dir[0],0,move_dir[1]};
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

void update(double time, double deltaTime, int width, int height, int nAudioFrames, int16_t *audioSamples){
	static bool init = false;
	if (!init){
		init = true;

		int w,h;
		uint32_t *p = load_image(true,&w,&h,"textures/blocks.png");
		glGenTextures(1,&block_texture_id);
		glBindTexture(GL_TEXTURE_2D,block_texture_id);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,p);
		free(p);

		init_light_coefficients();

		gen_world();
		mesh_world();
		lock_mouse(true);

		entity_set_position(&player,16,68,16);
	}

	accumulated_time += deltaTime;
	while (accumulated_time >= 1.0/20.0){
		accumulated_time -= 1.0/20.0;
		tick();
	}
	interpolant = accumulated_time / SEC_PER_TICK;

	//DRAW:	
	glViewport(0,0,width,height);

	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);
	//glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(90.0,(double)width/height,0.01,1000.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotated(player.head_rotation[0],1,0,0);
	glRotated(player.head_rotation[1],0,1,0);
	vec3 cam_pos;
	get_entity_interpolated_position(&player,cam_pos);
	cam_pos[1] += 1.62f-0.9f;
	vec3_negate(cam_pos,cam_pos);
	glTranslatef(cam_pos[0],cam_pos[1],cam_pos[2]);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(3,GL_FLOAT,sizeof(vertex_t),&world_opaque_mesh.elements[0].x);
	glTexCoordPointer(2,GL_FLOAT,sizeof(vertex_t),&world_opaque_mesh.elements[0].u);
	glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(vertex_t),&world_opaque_mesh.elements[0].r);
	glDrawArrays(GL_TRIANGLES,0,world_opaque_mesh.used);
	glDisable(GL_TEXTURE_2D);
}

int main(int argc, char **argv){
    open_window(640,480);
}