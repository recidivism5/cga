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
	{"brick",false,{{2,1},{2,1},{2,1},{2,1},{2,1},{2,1}}},
	{"yellow_light",false,{{3,2},{3,2},{3,2},{3,2},{3,2},{3,2}}},
	{"purple_light",false,{{3,3},{3,3},{3,3},{3,3},{3,3},{3,3}}},
	{"double_slab",false,{{4,0},{4,0},{4,1},{4,1},{4,0},{4,0}}},
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
	BLOCK_YELLOW_LIGHT,
	BLOCK_PURPLE_LIGHT,
	BLOCK_DOUBLE_SLAB
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

void vertex_list_reset(vertex_list_t *list){
	if (list->elements){
		free(list->elements);
	}
	list->elements = 0;
	list->total = 0;
	list->used = 0;
}

#define CHUNK_WIDTH 16
int chunk_radius = 2;

typedef struct {
	bool remesh;
	bool neighbors_exist[4];
	block_t blocks[CHUNK_WIDTH*CHUNK_WIDTH*CHUNK_WIDTH];
	vertex_list_t transparent_mesh;
	vertex_list_t opaque_mesh;
} chunk_t;

block_t *get_block_in_chunk(chunk_t *c, int x, int y, int z){
	return c->blocks + y*CHUNK_WIDTH*CHUNK_WIDTH + z*CHUNK_WIDTH + x;
}

void set_block_in_chunk(chunk_t *c, int x, int y, int z, int id){
	get_block_in_chunk(c,x,y,z)->id = id;
}

typedef struct chunk_bucket_s {
	struct chunk_bucket_s *prev, *next;
	ivec3 position;
	chunk_t *chunk;
} chunk_bucket_t;

typedef struct {
	int total, used, tombstones;
	chunk_bucket_t *buckets, *first, *last;
} chunk_hashlist_t;

#define TOMBSTONE UINTPTR_MAX

uint32_t fnv_1a(char *key, int len){
	uint32_t index = 2166136261u;
	for (int i = 0; i < len; i++){
		index ^= key[i];
		index *= 16777619;
	}
	return index;
}

chunk_bucket_t *chunk_hashlist_get(chunk_hashlist_t *list, ivec3 position){
	if (!list->total) return 0;
	uint32_t index = fnv_1a((char *)position,sizeof(ivec3)) % list->total;
	chunk_bucket_t *tombstone = 0;
	while (1){
		chunk_bucket_t *b = list->buckets+index;
		if ((uintptr_t)b->chunk == TOMBSTONE) tombstone = b;
		else if (b->chunk == 0) return tombstone ? tombstone : b;
		else if (!memcmp(b->position,position,sizeof(ivec3))) return b;
		index = (index + 1) % list->total;
	}
}

chunk_bucket_t *chunk_hashlist_get_checked(chunk_hashlist_t *list, ivec3 position){
	chunk_bucket_t *b = chunk_hashlist_get(list,position);
	if (!b || b->chunk == 0 || (uintptr_t)b->chunk == TOMBSTONE) return 0;
	return b;
}

void chunk_hashlist_remove(chunk_hashlist_t *list, chunk_bucket_t *b){
	b->chunk = (chunk_t *)TOMBSTONE;
	if (b->prev) b->prev->next = b->next;
	if (b->next) b->next->prev = b->prev;
	if (list->first == b) list->first = b->next;
	if (list->last == b) list->last = b->prev;
	list->used--;
	list->tombstones++;
}

chunk_bucket_t *chunk_hashlist_new(chunk_hashlist_t *list, ivec3 position){
	if ((list->used+list->tombstones+1) > (list->total*3)/4){ // 3/4 load limit, see benchmark/HashTableBenchmarkResults.txt
		chunk_hashlist_t newList;
		newList.total = 16; // same as used resize factor
		while (newList.total < (list->used*16)) newList.total *= 2; // 16x used resize, see benchmark/HashTableBenchmarkResults.txt
		newList.used = list->used;
		newList.tombstones = 0;
		newList.first = 0;
		newList.last = 0;
		newList.buckets = calloc(1,newList.total*sizeof(*newList.buckets));
		ASSERT(newList.buckets);
		for (chunk_bucket_t *b = list->first; b; b = b->next){
			chunk_bucket_t *nb = chunk_hashlist_get(&newList,b->position);
			memcpy(nb->position,b->position,sizeof(ivec3));
			nb->chunk = b->chunk;
			if (!newList.first){
				newList.first = nb;
				nb->prev = 0;
			} else {
				newList.last->next = nb;
				nb->prev = newList.last;
			}
			newList.last = nb;
			nb->next = 0;
		}
		if (list->buckets) free(list->buckets);
		*list = newList;
	}
	chunk_bucket_t *b = chunk_hashlist_get(list,position);
	memcpy(b->position,position,sizeof(ivec3));
	if (!list->first){
		list->first = b;
		b->prev = 0;
	} else {
		list->last->next = b;
		b->prev = list->last;
	}
	list->last = b;
	b->next = 0;
	if ((uintptr_t)b->chunk == TOMBSTONE) list->tombstones--;
	list->used++;
	return b;
}

void chunk_hashlist_reset(chunk_hashlist_t *list){
	if (list->buckets){
		free(list->buckets);
	}
	memset(list,0,sizeof(*list));
}

chunk_hashlist_t world, chunk_remesh_list;

void queue_remesh(int x, int y, int z){
	chunk_bucket_t *b = chunk_hashlist_get_checked(&world,(ivec3){x,y,z});
	if (b){
		if (!chunk_hashlist_get_checked(&chunk_remesh_list,(ivec3){x,y,z})){
			chunk_bucket_t *nb = chunk_hashlist_new(&chunk_remesh_list,(ivec3){x,y,z});
			nb->chunk = b->chunk;
		}
	}
}

void get_chunk_position(ivec3 world_pos, ivec3 chunk_pos){
	for (int i = 0; i < 3; i++){
		chunk_pos[i] = (world_pos[i] < 0 ? -1 : 0) + (world_pos[i] + (world_pos[i] < 0 ? 1 : 0)) / CHUNK_WIDTH;
	}
}

void get_chunk_position_vec3(vec3 world_pos, ivec3 chunk_pos){
	ivec3 v;
	for (int i = 0; i < 3; i++){
		v[i] = (int)floorf(world_pos[i]);
	}
	get_chunk_position(v,chunk_pos);
}

block_t *get_block(int x, int y, int z){
	ivec3 chunk_pos;
	get_chunk_position((ivec3){x,y,z},chunk_pos);
	chunk_bucket_t *b = chunk_hashlist_get_checked(&world,chunk_pos);
	if (b){
		ivec3 bp = {x,y,z};
		for (int i = 0; i < 3; i++){
			bp[i] = modulo(bp[i],CHUNK_WIDTH);
		}
		return get_block_in_chunk(b->chunk,bp[0],bp[1],bp[2]);
	} else return 0;
}

typedef struct {
	vec3 min,max;
} mmbb_t;

typedef struct {
	ivec3 min,max;
} immbb_t;

typedef struct {
	ivec3 positions[32*32*32];
	int write_index, read_index;
} ivec3_ring_t;

ivec3_ring_t light_prop_ring, dark_prop_ring;

void ivec3_ring_push(ivec3_ring_t *ring, int x, int y, int z){
	ring->positions[ring->write_index][0] = x;
	ring->positions[ring->write_index][1] = y;
	ring->positions[ring->write_index][2] = z;
	ring->write_index = (ring->write_index + 1) % COUNT(ring->positions);
	ASSERT(ring->write_index != ring->read_index);
}

int *ivec3_ring_pop(ivec3_ring_t *ring){
	if (ring->write_index == ring->read_index){
		return 0;
	}
	int *p = (int *)(ring->positions+ring->read_index);
	ring->read_index = (ring->read_index + 1) % COUNT(ring->positions);
	return p;
}

int get_yellow_light(block_t *b){
	return b->light >> 4;
}

int get_blue_light(block_t *b){
	return b->light & 0x0f;
}

void set_yellow_light(block_t *b, int val){
	b->light &= 0x0f;
	b->light |= val << 4;
}

void set_blue_light(block_t *b, int val){
	b->light &= 0xf0;
	b->light |= val;
}

/*void prop_light_neighbor(int x, int y, int z, int (*get_light)(block_t *), void (*set_light)(block_t *, int), int val, ivec3_ring_t *ring){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		if (get_light(b) < val){
			set_light(b,val);
			if (val > 1){
				ivec3_ring_push(ring,x,y,z);
			}
		}
	}
}*/

void prop_light_neighbor(int x, int y, int z, int yellow, int purple){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		if (get_yellow_light(b) < yellow){
			set_yellow_light(b,yellow);
			if (yellow > 1){
				ivec3_ring_push(&light_prop_ring,x,y,z);
			}
		}
		if (get_blue_light(b) < purple){
			set_blue_light(b,purple);
			if (purple > 1){
				ivec3_ring_push(&light_prop_ring,x,y,z);
			}
		}
	}
}

void prop_dark_neighbor(int x, int y, int z, int yellow, int purple){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		int ny = get_yellow_light(b);
		int np = get_blue_light(b);
		if (yellow > 0 && ny > 0){
			ivec3_ring_push(ny < yellow ? &dark_prop_ring : &light_prop_ring,x,y,z);
		}
		if (purple > 0 && np > 0){
			ivec3_ring_push(np < purple ? &dark_prop_ring : &light_prop_ring,x,y,z);
		}
	}
}

immbb_t light_prop_bounds;

void propagate_light(){
	int *p;
	while (p = ivec3_ring_pop(&light_prop_ring)){
		block_t *b = get_block(p[0],p[1],p[2]);
		if (b){
			for (int i = 0; i < 3; i++){
				if (p[i] > light_prop_bounds.max[i]){
					light_prop_bounds.max[i] = p[i];
				} else if (p[i] < light_prop_bounds.min[i]){
					light_prop_bounds.min[i] = p[i];
				}
			}
			int yellow = MAX(0,get_yellow_light(b)-1);
			int purple = MAX(0,get_blue_light(b)-1);
			prop_light_neighbor(p[0]-1,p[1],p[2],yellow,purple);
			prop_light_neighbor(p[0]+1,p[1],p[2],yellow,purple);
			prop_light_neighbor(p[0],p[1]-1,p[2],yellow,purple);
			prop_light_neighbor(p[0],p[1]+1,p[2],yellow,purple);
			prop_light_neighbor(p[0],p[1],p[2]-1,yellow,purple);
			prop_light_neighbor(p[0],p[1],p[2]+1,yellow,purple);
		}
	}
}

void propagate_dark(){
	for (int i = 0; i < 3; i++){
		light_prop_bounds.min[i] = 0;
		light_prop_bounds.max[i] = 0;
	}
	int *p;
	while (p = ivec3_ring_pop(&dark_prop_ring)){
		block_t *b = get_block(p[0],p[1],p[2]);
		if (b){
			for (int i = 0; i < 3; i++){
				if (p[i] > light_prop_bounds.max[i]){
					light_prop_bounds.max[i] = p[i];
				} else if (p[i] < light_prop_bounds.min[i]){
					light_prop_bounds.min[i] = p[i];
				}
			}
			int yellow = MAX(0,get_yellow_light(b));
			int purple = MAX(0,get_blue_light(b));
			prop_dark_neighbor(p[0]-1,p[1],p[2],yellow,purple);
			prop_dark_neighbor(p[0]+1,p[1],p[2],yellow,purple);
			prop_dark_neighbor(p[0],p[1]-1,p[2],yellow,purple);
			prop_dark_neighbor(p[0],p[1]+1,p[2],yellow,purple);
			prop_dark_neighbor(p[0],p[1],p[2]-1,yellow,purple);
			prop_dark_neighbor(p[0],p[1],p[2]+1,yellow,purple);
			b->light = 0;
		}
	}
	propagate_light();
	ivec3 cmin, cmax;
	get_chunk_position(light_prop_bounds.min,cmin);
	get_chunk_position(light_prop_bounds.max,cmax);
	for (int x = cmin[0]; x <= cmax[0]; x++){
		for (int y = cmin[1]; y <= cmax[1]; y++){
			for (int z = cmin[2]; z <= cmax[2]; z++){
				queue_remesh(x,y,z);
			}
		}
	}
}

void generate_chunk(chunk_t *c){
	for (int y = 0; y < CHUNK_WIDTH; y++){
		for (int z = 0; z < CHUNK_WIDTH; z++){
			for (int x = 0; x < CHUNK_WIDTH; x++){
				block_t *b = get_block_in_chunk(c,x,y,z);
				if (y < 4){
					b->id = BLOCK_BRICK;
				} else {
					b->id = BLOCK_AIR;
				}
				b->light = 0;
			}
		}
	}
	set_block_in_chunk(c,3,4,8,BLOCK_YELLOW_LIGHT);
	set_block_in_chunk(c,12,4,8,BLOCK_PURPLE_LIGHT);
}

void try_set_yellow_light(int x, int y, int z, int val){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		set_yellow_light(b,val);
		ivec3_ring_push(&light_prop_ring,x,y,z);
	}
}

void try_set_purple_light(int x, int y, int z, int val){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		set_blue_light(b,val);
		ivec3_ring_push(&light_prop_ring,x,y,z);
	}
}

void try_set_light(int x, int y, int z){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		ivec3_ring_push(&light_prop_ring,x,y,z);
	}
}

void try_set_dark(int x, int y, int z){
	block_t *b = get_block(x,y,z);
	if (b && block_types[b->id].transparent){
		ivec3_ring_push(&dark_prop_ring,x,y,z);
	}
}

void init_lights(chunk_bucket_t *c){
	for (int y = 0; y < CHUNK_WIDTH; y++){
		for (int z = 0; z < CHUNK_WIDTH; z++){
			for (int x = 0; x < CHUNK_WIDTH; x++){
				block_t *b = get_block_in_chunk(c->chunk,x,y,z);
				int wx = c->position[0] * CHUNK_WIDTH + x;
				int wy = c->position[1] * CHUNK_WIDTH + y;
				int wz = c->position[2] * CHUNK_WIDTH + z;
				if (b->id == BLOCK_YELLOW_LIGHT){
					try_set_yellow_light(wx-1,wy,wz,15);
					try_set_yellow_light(wx+1,wy,wz,15);
					try_set_yellow_light(wx,wy-1,wz,15);
					try_set_yellow_light(wx,wy+1,wz,15);
					try_set_yellow_light(wx,wy,wz-1,15);
					try_set_yellow_light(wx,wy,wz+1,15);
				} else if (b->id == BLOCK_PURPLE_LIGHT){
					try_set_purple_light(wx-1,wy,wz,15);
					try_set_purple_light(wx+1,wy,wz,15);
					try_set_purple_light(wx,wy-1,wz,15);
					try_set_purple_light(wx,wy+1,wz,15);
					try_set_purple_light(wx,wy,wz-1,15);
					try_set_purple_light(wx,wy,wz+1,15);
				}
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

float cube_vertices[] = {
	0,1,0, 0,0,0, 0,0,1, 0,0,1, 0,1,1, 0,1,0,
	1,1,1, 1,0,1, 1,0,0, 1,0,0, 1,1,0, 1,1,1,

	0,0,0, 1,0,0, 1,0,1, 1,0,1, 0,0,1, 0,0,0,
	1,1,0, 0,1,0, 0,1,1, 0,1,1, 1,1,1, 1,1,0,

	1,1,0, 1,0,0, 0,0,0, 0,0,0, 0,1,0, 1,1,0,
	0,1,1, 0,0,1, 1,0,1, 1,0,1, 1,1,1, 0,1,1,
};

void append_block_face(chunk_t *c, int x, int y, int z, int face_id, block_t *neighbor, block_type_t *type){
	vertex_t *v = vertex_list_make_room(type->transparent ? &c->transparent_mesh : &c->opaque_mesh, 6);
	float yellow = light_coefficients[get_yellow_light(neighbor)];
	float purple = light_coefficients[get_blue_light(neighbor)];
	float ambient = ambient_light_coefficients[face_id];
	uint8_t r = (uint8_t)(255 * ambient * yellow);
	uint8_t g = (uint8_t)(255 * ambient * MAX(yellow,purple));
	uint8_t b = (uint8_t)(255 * ambient * purple);
	for (int i = 0; i < 6; i++){
		v[i].x = x + cube_vertices[(face_id * 6 + i)*3 + 0];
		v[i].y = y + cube_vertices[(face_id * 6 + i)*3 + 1];
		v[i].z = z + cube_vertices[(face_id * 6 + i)*3 + 2];
		v[i].r = r;
		v[i].g = g;
		v[i].b = b;
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

chunk_bucket_t *local_chunks[6];

void test_neighbor(chunk_t *c, int x, int y, int z, int face_id, int axis, int dir, block_type_t *type){
	block_t *nb;
	ivec3 v = {x,y,z};
	if (v[axis] == 0 && dir == -1){
		if (!local_chunks[face_id]){
			return;
		}
		v[axis] = CHUNK_WIDTH-1;
		nb = get_block_in_chunk(local_chunks[face_id]->chunk,v[0],v[1],v[2]);
	} else if (v[axis] == (CHUNK_WIDTH-1) && dir == 1){
		if (!local_chunks[face_id]){
			return;
		}
		v[axis] = 0;
		nb = get_block_in_chunk(local_chunks[face_id]->chunk,v[0],v[1],v[2]);
	} else {
		v[axis] += dir;
		nb = get_block_in_chunk(c,v[0],v[1],v[2]);
	}
	if (nb){
		block_type_t *nbt = block_types+nb->id;
		if (nbt->transparent){
			append_block_face(c,x,y,z,face_id,nb,type);
		}
	}
}

void mesh_chunk(chunk_bucket_t *c){
	vertex_list_reset(&c->chunk->opaque_mesh);
	vertex_list_reset(&c->chunk->transparent_mesh);
	local_chunks[0] = chunk_hashlist_get_checked(&world,(ivec3){c->position[0]-1,c->position[1],c->position[2]});
	local_chunks[1] = chunk_hashlist_get_checked(&world,(ivec3){c->position[0]+1,c->position[1],c->position[2]});
	local_chunks[2] = chunk_hashlist_get_checked(&world,(ivec3){c->position[0],c->position[1]-1,c->position[2]});
	local_chunks[3] = chunk_hashlist_get_checked(&world,(ivec3){c->position[0],c->position[1]+1,c->position[2]});
	local_chunks[4] = chunk_hashlist_get_checked(&world,(ivec3){c->position[0],c->position[1],c->position[2]-1});
	local_chunks[5] = chunk_hashlist_get_checked(&world,(ivec3){c->position[0],c->position[1],c->position[2]+1});
	for (int y = 0; y < CHUNK_WIDTH; y++){
		for (int z = 0; z < CHUNK_WIDTH; z++){
			for (int x = 0; x < CHUNK_WIDTH; x++){
				block_t *b = get_block_in_chunk(c->chunk,x,y,z);
				block_type_t *bt = block_types+b->id;
				if (b->id){
					test_neighbor(c->chunk,x,y,z,0,0,-1,bt);
					test_neighbor(c->chunk,x,y,z,1,0,+1,bt);
					test_neighbor(c->chunk,x,y,z,2,1,-1,bt);
					test_neighbor(c->chunk,x,y,z,3,1,+1,bt);
					test_neighbor(c->chunk,x,y,z,4,2,-1,bt);
					test_neighbor(c->chunk,x,y,z,5,2,+1,bt);
				}
			}
		}
	}
}

void mesh_chunks(){
	for (chunk_bucket_t *b = chunk_remesh_list.first; b; b = b->next){
		mesh_chunk(b);
	}
	chunk_hashlist_reset(&chunk_remesh_list);
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

	for (int y = im.min[1]; y <= im.max[1]; y++){
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

typedef struct {
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
}

void get_player_head_pos(vec3 out){
	get_entity_interpolated_position(&player,out);
	out[1] += 1.62f-0.9f;
}

void get_player_target_block(block_raycast_result_t *result){
	vec3 head;
	get_player_head_pos(head);
	vec3 ray = {0,0,-5};
	vec3_rotate_deg(ray,(vec3){1,0,0},-player.head_rotation[0],ray);
	vec3_rotate_deg(ray,(vec3){0,1,0},-player.head_rotation[1],ray);
	cast_ray_into_blocks(head,ray,result);
}

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
		case KEY_MOUSE_LEFT:{
			block_raycast_result_t brr;
			get_player_target_block(&brr);
			if (brr.block){
				if (brr.block->id == BLOCK_YELLOW_LIGHT || brr.block->id == BLOCK_PURPLE_LIGHT){
					try_set_dark(brr.block_pos[0]-1,brr.block_pos[1],brr.block_pos[2]);
					try_set_dark(brr.block_pos[0]+1,brr.block_pos[1],brr.block_pos[2]);
					try_set_dark(brr.block_pos[0],brr.block_pos[1]-1,brr.block_pos[2]);
					try_set_dark(brr.block_pos[0],brr.block_pos[1]+1,brr.block_pos[2]);
					try_set_dark(brr.block_pos[0],brr.block_pos[1],brr.block_pos[2]-1);
					try_set_dark(brr.block_pos[0],brr.block_pos[1],brr.block_pos[2]+1);
				} else {
					try_set_light(brr.block_pos[0]-1,brr.block_pos[1],brr.block_pos[2]);
					try_set_light(brr.block_pos[0]+1,brr.block_pos[1],brr.block_pos[2]);
					try_set_light(brr.block_pos[0],brr.block_pos[1]-1,brr.block_pos[2]);
					try_set_light(brr.block_pos[0],brr.block_pos[1]+1,brr.block_pos[2]);
					try_set_light(brr.block_pos[0],brr.block_pos[1],brr.block_pos[2]-1);
					try_set_light(brr.block_pos[0],brr.block_pos[1],brr.block_pos[2]+1);
				}
				brr.block->id = BLOCK_AIR;
				brr.block->light = 0;
				propagate_dark();
			}
			break;
		}
		case KEY_MOUSE_RIGHT:{
			block_raycast_result_t brr;
			get_player_target_block(&brr);
			if (brr.block){
				int x = brr.block_pos[0]+brr.face_normal[0];
				int y = brr.block_pos[1]+brr.face_normal[1];
				int z = brr.block_pos[2]+brr.face_normal[2];
				block_t *b = get_block(x,y,z);
				if (b && !b->id){
					b->id = BLOCK_YELLOW_LIGHT;
					try_set_yellow_light(x-1,y,z,15);
					try_set_yellow_light(x+1,y,z,15);
					try_set_yellow_light(x,y-1,z,15);
					try_set_yellow_light(x,y+1,z,15);
					try_set_yellow_light(x,y,z-1,15);
					try_set_yellow_light(x,y,z+1,15);
				}
				propagate_dark();
			}
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

void tick(){
	ivec3 chunk_pos;
	get_chunk_position_vec3(player.current_position,chunk_pos);
	for (int x = -chunk_radius; x <= chunk_radius; x++){
		for (int y = -chunk_radius; y <= chunk_radius; y++){
			for (int z = -chunk_radius; z <= chunk_radius; z++){
				ivec3 new_chunk_pos = {chunk_pos[0]+x,chunk_pos[1]+y,chunk_pos[2]+z};
				chunk_bucket_t *b = chunk_hashlist_get_checked(&world,new_chunk_pos);
				if (!b){
					b = chunk_hashlist_new(&world,new_chunk_pos);
					for (chunk_bucket_t *oldb = world.first; oldb; oldb = oldb->next){
						if (
							abs(oldb->position[0]-chunk_pos[0]) > chunk_radius ||
							abs(oldb->position[1]-chunk_pos[1]) > chunk_radius ||
							abs(oldb->position[2]-chunk_pos[2]) > chunk_radius){
							b->chunk = oldb->chunk;
							chunk_hashlist_remove(&world,oldb);
							goto L1;
						}
					}
					b->chunk = calloc(1,sizeof(chunk_t));
					ASSERT(b->chunk);
					L1:;
					generate_chunk(b->chunk);
					init_lights(b);
					queue_remesh(b->position[0],b->position[1],b->position[2]);
					queue_remesh(b->position[0]-1,b->position[1],b->position[2]);
					queue_remesh(b->position[0]+1,b->position[1],b->position[2]);
					queue_remesh(b->position[0],b->position[1]-1,b->position[2]);
					queue_remesh(b->position[0],b->position[1]+1,b->position[2]);
					queue_remesh(b->position[0],b->position[1],b->position[2]-1);
					queue_remesh(b->position[0],b->position[1],b->position[2]+1); // queue this chunk and its neighbors for remesh
					goto L2; //limit to one chunk per tick
				}
			}
		}
	}
	L2:;
	//propagate_dark();
	propagate_light();
	mesh_chunks();

	if (chunk_hashlist_get_checked(&world,chunk_pos)){
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
}

#define TEXT_IMG_WIDTH 256
uint32_t textImg[TEXT_IMG_WIDTH*TEXT_IMG_WIDTH];
GLuint textImgTid;

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

		glGenTextures(1,&textImgTid);
		glBindTexture(GL_TEXTURE_2D,textImgTid);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);

		init_light_coefficients();

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
	block_raycast_result_t brr;
	get_player_target_block(&brr);
	ivec3 chunk_pos;
	get_chunk_position_vec3(cam_pos,chunk_pos);

	//DRAW:	
	glViewport(0,0,width,height);

	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

	glBindTexture(GL_TEXTURE_2D,block_texture_id);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(90.0,(double)width/height,0.01,1000.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotated(player.head_rotation[0],1,0,0);
	glRotated(player.head_rotation[1],0,1,0);
	glTranslatef(-cam_pos[0],-cam_pos[1],-cam_pos[2]);

	if (brr.block){
		glPushMatrix();
		glTranslatef((float)brr.block_pos[0],(float)brr.block_pos[1],(float)brr.block_pos[2]);
		glScaled(1.005,1.005,1.005);
		glTranslated(-0.0025,-0.0025,-0.0025);
		glColor3d(0,0,0);
		glBegin(GL_LINES);
			glVertex3d(0,1,0); glVertex3d(0,0,0);
			glVertex3d(0,0,0); glVertex3d(0,0,1);
			glVertex3d(0,0,1); glVertex3d(0,1,1);
			glVertex3d(0,1,1); glVertex3d(0,1,0);
			glVertex3d(1,1,0); glVertex3d(1,0,0);
			glVertex3d(1,0,0); glVertex3d(1,0,1);
			glVertex3d(1,0,1); glVertex3d(1,1,1);
			glVertex3d(1,1,1); glVertex3d(1,1,0);
			glVertex3d(0,1,0); glVertex3d(1,1,0);
			glVertex3d(0,0,0); glVertex3d(1,0,0);
			glVertex3d(0,0,1); glVertex3d(1,0,1);
			glVertex3d(0,1,1); glVertex3d(1,1,1);
		glEnd();
		glColor3d(1,1,1);
		glPopMatrix();
	}

	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	for (chunk_bucket_t *b = world.first; b; b = b->next){
		glPushMatrix();
		glTranslatef((float)b->position[0]*CHUNK_WIDTH,(float)b->position[1]*CHUNK_WIDTH,(float)b->position[2]*CHUNK_WIDTH);
		glVertexPointer(3,GL_FLOAT,sizeof(vertex_t),&b->chunk->opaque_mesh.elements[0].x);
		glTexCoordPointer(2,GL_FLOAT,sizeof(vertex_t),&b->chunk->opaque_mesh.elements[0].u);
		glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(vertex_t),&b->chunk->opaque_mesh.elements[0].r);
		glDrawArrays(GL_TRIANGLES,0,b->chunk->opaque_mesh.used);
		glPopMatrix();
	}
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	//fuck this shit
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,width,0,height,-1,100);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	memset(textImg,0,sizeof(textImg));
	text_set_target_image(textImg,TEXT_IMG_WIDTH,TEXT_IMG_WIDTH);
	text_set_color(1,0,1);
	wchar_t tbuf[512];
	wchar_t fname[33];
	mbstowcs(fname,get_font_name("comic.ttf"),COUNT(fname));
	_snwprintf(tbuf,COUNT(tbuf),L"TinyCraft Alpha\nKeyboard: %s\ncam_pos: %f %f %f\nchunk_pos: %d %d %d\n%s",get_keyboard_layout_name(),cam_pos[0],cam_pos[1],cam_pos[2],chunk_pos[0],chunk_pos[1],chunk_pos[2],fname);
	text_draw(0,TEXT_IMG_WIDTH,0,TEXT_IMG_WIDTH,tbuf);
	for (int i = 0; i < TEXT_IMG_WIDTH*TEXT_IMG_WIDTH; i++){
		uint8_t *p = (uint8_t *)(textImg + i);
		p[3] = p[0];
	}
	glBindTexture(GL_TEXTURE_2D,textImgTid);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,TEXT_IMG_WIDTH,TEXT_IMG_WIDTH,0,GL_RGBA,GL_UNSIGNED_BYTE,textImg);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0); glVertex2f(0,height-(float)TEXT_IMG_WIDTH);
	glTexCoord2f(1,0); glVertex2f(TEXT_IMG_WIDTH,height-(float)TEXT_IMG_WIDTH);
	glTexCoord2f(1,1); glVertex2f(TEXT_IMG_WIDTH,(float)height);
	glTexCoord2f(0,1); glVertex2f(0,(float)height);
	glEnd();
	glDisable(GL_TEXTURE_2D);
}

int main(int argc, char **argv){
    open_window(640,480);
}