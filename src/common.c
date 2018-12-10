#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define CLAMP_MAX(x, max) MIN(x, max)
#define CLAMP_MIN(x, min) MAX(x, min)
#define IS_POW2(x) (((x) != 0) && ((x) & ((x)-1)) == 0)

//generate random float numbers based on c stdlib rnd() function
#define RND() ((float)rand() / (float)(RAND_MAX))
#define RNDX(x) (RND()*(x))
#define RNDRNG(x, y) (RNDX((y) - (x)) + (x))

#define ALIGN_DOWN(n, a) ((n) & ~((a) - 1))
#define ALIGN_UP(n, a) ALIGN_DOWN((n) + (a) - 1, (a))
#define ALIGN_DOWN_PTR(p, a) ((void *)ALIGN_DOWN((uintptr_t)(p), (a)))
#define ALIGN_UP_PTR(p, a) ((void *)ALIGN_UP((uintptr_t)(p), (a)))


void fatal(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	printf("FATAL: ");
	vprintf(fmt, args);
	printf("\n");
	va_end(args);
	exit(1);
}

//calloc sets the memory to zero
void *xcalloc(size_t num_elems, size_t elem_size) {
	void *ptr = calloc(num_elems, elem_size);
	if (!ptr) {
		perror("xcalloc failed");
		exit(1);
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t num_bytes) {
	ptr = realloc(ptr, num_bytes);
	if (!ptr) {
		perror("xrealloc failed");
		exit(1);
	}
	return ptr;
}

void *xmalloc(size_t new_size) {
	void *ptr = malloc(new_size);
	if (!ptr) {
		perror("xmalloc failed");
		exit(1);
	}
	return ptr;
}

void *memdup(void *src, size_t size) {
	void *dest = xmalloc(size);
	memcpy(dest, src, size);
	return dest;
}


char *strf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	size_t n = 1 + vsnprintf(NULL, 0, fmt, args);
	char *str = xmalloc(n);
	va_start(args, fmt);
	vsnprintf(str, n, fmt, args);
	va_end(args);
	return str;
}


//
//Stretchy Buff: Dynamic arrays 
//

typedef struct BufHdr {
	size_t len;
	size_t cap;
	char buf[];
} BufHdr;

//#define offsetof(s,m) ((size_t)&(((s*)0)->m))
#define buf__hdr(b) ((BufHdr *)((char *)(b) - offsetof(BufHdr, buf)))

#define buf_len(b) ((b) ? buf__hdr(b)->len : 0)
#define buf_cap(b) ((b) ? buf__hdr(b)->cap : 0)
#define buf_end(b) ((b) + buf_len(b))
#define buf_sizeof(b) ((b) ? buf_len(b) * sizeof(*(b)): 0)

#define buf_free(b) ((b) ? free(buf__hdr(b)), (b) = NULL : 0)
#define buf_fit(b, n) ((n) <= buf_cap(b) ? 0 : ((b) = buf_grow((b), (n), sizeof(*(b)))))
#define buf_push(b, ...) (buf_fit((b), 1 + buf_len(b)), (b)[buf__hdr(b)->len++] = (__VA_ARGS__))
#define buf_printf(b, ...) ((b) = buf__printf((b), __VA_ARGS__))
#define buf_clear(b) = ((b) ? buf__hdr(b)->len = 0: 0)

void *buf_grow(const void *buf, size_t new_len, size_t elem_size) {
	//buf_cap(buf) * 2 + 1 <= SIZE_MAX
	assert(buf_cap(buf) <= (SIZE_MAX - 1) / 2);
	size_t new_cap = CLAMP_MIN(2*buf_cap(buf), MAX(new_len, 16));
	assert(new_len <= new_cap);
	//new_cap * elem_size + offsetof(BufHdr, buf) <= SIZE_MAX
	assert(new_cap <= (SIZE_MAX - offsetof(BufHdr, buf)) / elem_size);
	size_t new_size = offsetof(BufHdr, buf) + new_cap * elem_size;
	BufHdr *new_hdr;
	if (buf) {
		new_hdr = xrealloc(buf__hdr(buf), new_size);
	}
	else {
		new_hdr = xmalloc(new_size);
		new_hdr->len = 0;
	}
	new_hdr->cap = new_cap;
	return new_hdr->buf;
}

char *buf__printf(char *buf, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	size_t cap = buf_cap(buf) - buf_len(buf);
	size_t n = 1 + vsnprintf(buf_end(buf), cap, fmt, args);
	if (n > cap) {
		buf_fit(buf, n + buf_len(buf));
		va_start(args, fmt);
		size_t new_cap = buf_cap(buf) - buf_len(buf);
		n = 1 + vsnprintf(buf_end(buf), new_cap, fmt, args);
		assert(n <= new_cap);
		va_end(args);
	}
	buf__hdr(buf)->len += n - 1;
	return buf;
}

//
// Arena allocator
//

typedef struct Arena {
	char *ptr;
	char *end;
	char **blocks;
} Arena;

#define ARENA_ALIGNMENT 8
#define ARENA_BLOCK_SIZE (1024 * 1024)

void arena_grow(Arena *arena, size_t min_size) {
	size_t size = ALIGN_UP(MAX(ARENA_BLOCK_SIZE, min_size), ARENA_ALIGNMENT);
	arena->ptr = xmalloc(size);
	assert(arena->ptr == ALIGN_DOWN_PTR(arena->ptr, ARENA_ALIGNMENT));
	arena->end = arena->ptr + size;
	buf_push(arena->blocks, arena->ptr);
}

void *arena_alloc(Arena *arena, size_t size) {
	if (size > (size_t)(arena->end - arena->ptr)) {
		arena_grow(arena, size);
		assert(size <= (size_t)(arena->end - arena->ptr));
	}
	void *ptr = arena->ptr;
	arena->ptr = ALIGN_UP_PTR(arena->ptr + size, ARENA_ALIGNMENT);
	assert(arena->ptr <= arena->end);
	assert(ptr == ALIGN_DOWN_PTR(ptr, ARENA_ALIGNMENT));
	return ptr;
}

void arena_free(Arena *arena) {
	for (char **it = arena->blocks; it != buf_end(arena->blocks); it++) {
		free(*it);
	}
	buf_free(arena->blocks);
}


//
// Hash Map
//


uint64_t hash_uint64(uint64_t x) {
	x *= 0xff51afd7ed558ccd;
	x ^= x >> 32;
	return x;
}

uint64_t hash_ptr(void *ptr) {
	return hash_uint64((uintptr_t)ptr);
}

uint64_t hash_mix(uint64_t x, uint64_t y) {
	x ^= y;
	x *= 0xff51afd7ed558ccd;
	x ^= x >> 32;
	return x;
}

uint64_t hash_bytes(const void *ptr, size_t len) {
	uint64_t x = 0xcbf29ce484222325;
	const char *buf = (const char *)ptr;
	for (size_t i = 0; i < len; i++) {
		x ^= buf[i];
		x *= 0x100000001b3;
		x ^= x >> 32;
	}
	return x;
}

typedef struct Map {
	uint64_t *keys;
	uint64_t *vals;
	size_t len;
	size_t cap;
} Map;

uint64_t map_get_uint64_from_uint64(Map *map, uint64_t key) {
	if (map->len == 0) {
		return 0;
	}
	assert(IS_POW2(map->cap));
	size_t i = (size_t)hash_uint64(key);
	assert(map->len < map->cap);
	for (;;) {
		i &= map->cap - 1;
		if (map->keys[i] == key) {
			return map->vals[i];
		}
		else if (!map->keys[i]) {
			return 0;
		}
		i++;
	}
	return 0;
}

void map_put_uint64_from_uint64(Map *map, uint64_t key, uint64_t val);

void map_grow(Map *map, size_t new_cap) {
	new_cap = CLAMP_MIN(16, new_cap);
	Map new_map = {
		.keys = xcalloc(new_cap, sizeof(uint64_t)),
		.vals = xmalloc(new_cap * sizeof(uint64_t)),
		.cap = new_cap,
	};
	for (size_t i = 0; i < map->cap; i++) {
		if (map->keys[i]) {
			map_put_uint64_from_uint64(&new_map, map->keys[i], map->vals[i]);
		}
	}
	free((void *)map->keys);
	free(map->vals);
	*map = new_map;
}

void map_put_uint64_from_uint64(Map *map, uint64_t key, uint64_t val) {
	assert(key);
	if (!val) {
		return;
	}
	if (2*map->len >= map->cap) {
		map_grow(map, 2 * map->cap);
	}
	assert(2 * map->len < map->cap);
	assert(IS_POW2(map->cap));
	size_t i = (size_t)hash_uint64(key);
	for (;;) {
		i &= map->cap - 1; // reduced to number between 0-15
		if (!map->keys[i]) {
			map->len++;
			map->keys[i] = key;
			map->vals[i] = val;
			return;
		}
		else if (map->keys[i] == key) {
			map->vals[i] = val;
			return;
		}
		i++;
	}
}

void *map_get(Map *map, const void *key) {
	return (void *)(uintptr_t)map_get_uint64_from_uint64(map, (uint64_t)(uintptr_t)key);
}

void *map_get_from_uint64(Map *map, uint64_t key) {
	return (void *)(uintptr_t)map_get_uint64_from_uint64(map, key);
}

uint64_t map_get_uint64(Map *map, void *key) {
	return map_get_uint64_from_uint64(map, (uint64_t)(uintptr_t)key);
}


void map_put(Map *map, const void *key, void *val) {
	map_put_uint64_from_uint64(map, (uint64_t)(uintptr_t)key, (uint64_t)(uintptr_t)val);
}

void map_put_from_uint64(Map *map, uint64_t key, void *val) {
	map_put_uint64_from_uint64(map, key, (uint64_t)(uintptr_t)val);
}

void map_put_uint64(Map *map, void *key, uint64_t val) {
	map_put_uint64_from_uint64(map, (uint64_t)(uintptr_t)key, val);
}



//
//String Interning
//

typedef struct Intern {
	size_t len;
	struct Intern *next;
	char str[];
} Intern;

Arena intern_arena;
Map interns;

const char *str_intern_range(const char *start, const char *end) {
	size_t len = end - start;
	uint64_t hash = hash_bytes(start, len);
	uint64_t key = hash ? hash : 1;
	Intern *intern = map_get_from_uint64(&interns, key);
	for (Intern *it = intern; it; it = it->next) {
		if (it->len == len && strncmp(it->str, start, len) == 0) {
			return it->str;
		}
	}
	Intern *new_intern = arena_alloc(&intern_arena, offsetof(Intern, str) + len + 1);
	new_intern->len = len;
	new_intern->next = intern;
	memcpy(new_intern->str, start, len);
	new_intern->str[len] = 0;
	map_put_from_uint64(&interns, key, new_intern);
	return new_intern->str;
}

const char *str_intern(const char *str) {
	return str_intern_range(str, str + strlen(str));
}
