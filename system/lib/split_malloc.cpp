/*
   malloc/free for SPLIT_MEMORY
*/

#include <stdlib.h>
#include <unistd.h>

#include <emscripten.h>

extern "C" {

typedef void* mspace;

mspace create_mspace_with_base(void* base, size_t capacity, int locked);
void* mspace_malloc(mspace space, size_t size);
void mspace_free(mspace space, void* ptr);

void* sbrk(intptr_t increment) {
  EM_ASM({
    Module.printErr("sbrk() should never be called when SPLIT_MEMORY!");
  });
}

}

#define MAX_SPACES 1000

static bool initialized = false;
static size_t split_memory = 0;
static size_t num_spaces = 0;
static mspace spaces[MAX_SPACES]; // 0 is for the stack, static, etc - not used by malloc # TODO: make a small space in there?

void init() {
  split_memory = EM_ASM_INT_V({
    return SPLIT_MEMORY;
  });
  num_spaces = EM_ASM_INT_V({
    return HEAPU8s.length;
  });
  if (num_spaces >= MAX_SPACES) abort();
  spaces[0] = 0;
  for (int i = 1; i < num_spaces; i++) {
    spaces[i] = create_mspace_with_base((void*)(split_memory*i), split_memory, 0);
  }
  initialized = true;
}

// TODO: optimize, these are powers of 2
#define space_index(ptr) (((unsigned)ptr) / split_memory)
#define space_relative(ptr) (((unsigned)ptr) % split_memory)

extern "C" {

void* malloc(size_t size) {
  if (!initialized) {
    init();
  }
  if (size >= split_memory) {
    static bool warned = false;
    if (!warned) {
      EM_ASM_({
        Module.print("trying to malloc " + $0 + ", a size larger than SPLIT_MEMORY (" + $1 + "), increase SPLIT_MEMORY if you want that to work");
      }, size, split_memory);
      warned = true;
    }
    return 0;
  }
  static int next = 1;
  int start = next;
  while (1) { // simple round-robin, while keeping to use the same one as long as it keeps succeeding
    void *ret = mspace_malloc(spaces[next], size);
    if (ret) return ret;
    next++;
    if (next == num_spaces) next = 1;
    if (next == start) break;
  }
  return 0; // we cycled, so none of them can allocate
}

void free(void* ptr) {
  if (!initialized) abort();
  unsigned index = space_index(ptr);
  if (index == 0 || index >= num_spaces) {
    fprintf(stderr, "bad free ptr %p\n", ptr);
    abort();
  }
  mspace_free(spaces[index], ptr);
}

}
