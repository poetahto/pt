/* 
    ptm_map - v0.01 - public domain `.map` loader
                            No warranty implied; use at your own risk

    Do this:
      #define PT_MAP_IMPLEMENTATION
    before you include this file in *one* C or C++ file to 
    create the implementation.

    BASIC USAGE:
      pt_map* map = ptm_load("example.map");

      for (int eid = 0; eid < map->entity_count; eid++) {
        ptm_entity* entity = &map->entities[eid];

        for (int pid = 0; pid < entity->property_count; pid++) {
          ptm_property* property = &entity->properties[pid];
        }

        for (int bid = 0; bid < entity->brush_count; bid++) {
          ptm_brush* brush = &entity->brushes[bid];

          for (int fid = 0; fid < brush->face_count; fid++) {
            ptm_face* face = &brush->faces[fid];
          }
        }
      }

      ptm_free(map);

    OPTIONS:
      #define PTM_ASSERT(expr)
        change how assertions are enforced

      #define PTM_MALLOC/FREE/REALLOC
        change allocation strategy

      #define PTM_HASH/CREATE_HASH
        change hash type/strategy

      #define PTM_REAL <float|double|custom>
        change precision of all real numbers (default to double)

      #define PTM_STRTOR
        re-define to change how real numbers are parsed, defaults 
        to strtod/strtof. this can be a bottleneck: consider 
        replacing for better performance

    BACKGROUND:
      ".map" files define brush-based levels for games in a simple, 
      plaintext format. They were originally used in the first quake 
      game, and later modified for its descendants (half-life, ect.)

      While brush-based level design has fallen out of favor in many 
      games, it remains a powerful prototyping and blockout tool, 
      along with having a retro aesthetic. There are also powerful 
      ".map" editors that are very mature, such as Trenchbroom.

      A map file is structured to not just define the visual layout of 
      a scene, but also the gameplay properties. Indeed, a map file is 
      nothing more than a list of entities which may or may not be 
      associated with a mesh (think: lights, player spawn positions, 
      ambient noise emitters). These are all defined by key-value 
      pairs which can be interpreted by the engine.

      The contents of a map file must be processed in several steps
      before they can be rendered in a game.

      1) Brushes, which are defined by lists of clippng planes.
      Intuitive to edit, impossible to render.

      2) Geometry, which is defined by lists of faces that contain
      edges, vertices, and texture info. Can be useful when editing,
      still impossible to render due to lack of triangles.

      3) Models, which are lists of meshes: each of which containing
      a list of vertex positions, triangle indices, and a texture.
      Not the most editable, but finally possible to send to GPU.

    REFERENCE:
      https://book.leveldesignbook.com/appendix/resources/formats/map
      https://github.com/stefanha/map-files

    LICENSE:
      This software is dual-licensed to the public domain and under 
      the following license: you are granted a perpetual, irrevocable 
      license to copy, modify, publish, and distribute this file 
      as you see fit.
            
    TODO::
      make more stuff live
        in arenas instead of linked lists (or move linked lists
        into arenas) for better performance, easier freeing
      
      Merge `func_group` into worldspawn on scope decrease

      Feature parity with that cool really old repository
        (CSG union, meshing, texture uv calculation)

      Other cool map-related things (bsps, collision hulls, 
        vis/portals, quake movement?)

      Another STB-style library for WAD loading would be cool!
        Or if we embed it into this for one an all-in-one 
        quick 3d prototype tool
 */

#ifndef PTM_MAP_H
#define PTM_MAP_H
/* === BEGIN HEADER === */

#define PTM_REAL float
#define PTM_HASH unsigned int

typedef struct ptm_brush_face {
  PTM_REAL plane_normal[3];
  PTM_REAL plane_c;
  const char* texture_name;
  PTM_REAL texture_uv[2][3];
  PTM_REAL texture_offset[2];
  PTM_REAL texture_scale[2];
  PTM_REAL texture_rotation;
} ptm_brush_face;

typedef struct ptm_brush {
  ptm_brush_face* faces;
  int face_count;
} ptm_brush;

typedef struct ptm_entity {
  char** property_keys;
  char** property_values;
  int property_count;
  ptm_brush* brushes;
  int brush_count;
} ptm_entity;

typedef struct ptm_map {
  ptm_entity* entities;
  int entity_count;

  struct ptm__arena* _arena;
  struct ptm__arena* _string_arena;
} ptm_map;

#ifndef PTM_NO_STDIO
ptm_map* ptm_load(const char* file_path);
#endif 

ptm_map* ptm_load_source(const char* source, int source_len);
void ptm_free(ptm_map* map);

/* === END HEADER === */
#endif // PTM_MAP_H

#ifdef PT_MAP_IMPLEMENTATION
/* === BEGIN IMPLEMENTATION === */

#include <stdint.h> // for ptrdiff_t with string parsing
#include <stddef.h> // for intptr_t, with string parsing

#ifndef PTM_ASSERT
#include <assert.h>
#define PTM_ASSERT(expr) assert(expr)
#endif

#ifndef PTM_MALLOC
#include <stdlib.h>
#define PTM_MALLOC(bytes) malloc(bytes)
#define PTM_FREE(buffer) free(buffer)
#define PTM_REALLOC(buffer, bytes) realloc(buffer, bytes)
#endif

#ifndef PTM_CREATE_HASH
#define PTM_CREATE_HASH(data, size) ptm__create_hash_fnv32(data, size)
PTM_HASH ptm__create_hash_fnv32(const char* data, int size) {
  unsigned int hash = 2166136261u;

  for (int i = 0; i < size; i++) {
    hash ^= *data++;
    hash *= 16777619u;
  }

  return hash;
}
#endif

#ifndef PTM_STRTOR
#include <stdlib.h>
#define PTM_STRTOR(start, end) ptm__strtor(start, end)
PTM_REAL ptm__strtor(const char* start, const char** end) {
#if PTM_REAL == double
  return strtod(start, (char**)end);
#elif PTM_REAL == float
  return strtof(start, (char**)end);
#else
  #error Value of PTM_REAL has no default implementation.
#endif
}
#endif

static void ptm__subtract_vec3(const float* a, const float* b, float* r) {
  r[0] = a[0] - b[0];
  r[1] = a[1] - b[1];
  r[2] = a[2] - b[2];
}

static float ptm__dot_vec3(const float* a, const float* b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void ptm__cross_vec3(const float* a, const float* b, float* r) {
  r[0] = a[1] * b[2] - a[2] * b[1];
  r[1] = a[2] * b[0] - a[0] * b[2];
  r[2] = a[0] * b[1] - a[1] * b[0];
}

static void ptm__copy_memory(void* dest, const void* src, int bytes) {
  char* d = (char*)dest;
  const char* s = (const char*)src;

  for (int i = 0; i < bytes; i++)
    d[i] = s[i];
}

static void ptm__zero_memory(void* memory, int bytes) {
  char* m = (char*)memory;

  for (int i = 0; i < bytes; i++)
    m[i] = 0;
}

typedef struct ptm__arena_chunk {
  struct ptm__arena_chunk* next;
  void* data;
  int head;
} ptm__arena_chunk;

typedef struct ptm__arena {
  ptm__arena_chunk* chunk_list;
  ptm__arena_chunk* current_chunk;
  int chunk_capacity;
} ptm__arena;

static void ptm__add_arena_chunk(ptm__arena* arena) {
  int size = sizeof(ptm__arena_chunk);
  ptm__arena_chunk* chunk = (ptm__arena_chunk*)PTM_MALLOC(size);
  chunk->data = PTM_MALLOC(arena->chunk_capacity);
  chunk->head = 0;
  chunk->next = NULL; 

  if (arena->current_chunk == NULL) {
    arena->chunk_list = chunk;
    arena->current_chunk = chunk;
  }
  else {
    arena->current_chunk->next = chunk;
    arena->current_chunk = chunk;
  }
}

static ptm__arena* ptm__create_arena(int chunk_capacity) {
  ptm__arena* arena = PTM_MALLOC(sizeof(ptm__arena));
  arena->chunk_capacity = chunk_capacity;
  arena->chunk_list = NULL;
  arena->current_chunk = NULL;
  ptm__add_arena_chunk(arena);
  return arena;
}

static void* ptm__arena_alloc(ptm__arena* arena, int bytes) {
  PTM_ASSERT(bytes < arena->chunk_capacity);

  if (arena->current_chunk->head + bytes > arena->chunk_capacity) {
    ptm__add_arena_chunk(arena);
  }

  ptm__arena_chunk* chunk = arena->current_chunk;
  void* result = ((char*)chunk->data) + chunk->head;
  chunk->head += bytes;
  return result;
}

static void ptm__free_arena(ptm__arena* arena) {
  ptm__arena_chunk* chunk = arena->chunk_list;

  while (chunk != NULL) {
    PTM_FREE(chunk->data);
    ptm__arena_chunk* next = chunk->next;
    PTM_FREE(chunk);
    chunk = next;
  }

  PTM_FREE(arena);
}

static void ptm__consume_until_at(const char** head, char value) {
  while (**head != value)
    (*head)++;
}

static void ptm__consume_until_after(const char** head, char value) {
  ptm__consume_until_at(head, value);
  (*head)++;
}

static void ptm__consume_whitespace(const char** head) {
  while (**head == ' ')
    (*head)++;
}

static PTM_REAL ptm__consume_number(const char** head) {
  const char* end;
  const char* start = *head;
  PTM_REAL value = PTM_STRTOR(start, &end);
  *head = end;
  return value;
}

typedef struct ptm__string_cache_node {
  struct ptm__string_cache_node* next;
  char* value;
  PTM_HASH hash;
}ptm__string_cache_node;

typedef struct ptm__string_cache {
  ptm__string_cache_node* node_list;
  ptm__string_cache_node* node_head;
  ptm__arena* node_arena;
  ptm__arena* value_arena;
}ptm__string_cache;

static char* ptm__consume_string(const char** head, char value, 
        ptm__string_cache* cache) 
{
  ptm__consume_until_after(head, value);
  const char* start = *head;
  ptm__consume_until_at(head, value);
  const char* end = *head;
  (*head)++;

  ptrdiff_t length = (intptr_t)end - (intptr_t)start;

  PTM_HASH hash = PTM_CREATE_HASH(start, length);

  // Check for a cache hit and return if so
  ptm__string_cache_node* node = cache->node_list;

  while (node != NULL) {
    if (node->hash == hash) {
      return node->value;
    }
    node = node->next;
  }

  // Cache did not contain string: alloc new one
  int size = sizeof(ptm__string_cache_node);

  node = (ptm__string_cache_node*)
      ptm__arena_alloc(cache->node_arena, size);

  node->next = NULL;
  node->hash = hash;
  node->value = (char*)ptm__arena_alloc(cache->value_arena, length + 1);
  ptm__copy_memory(node->value, start, length);
  node->value[length] = '\0';

  // Update cache linked list with the new string
  if (cache->node_head != NULL) {
    cache->node_head->next = node;
    cache->node_head = node;
  }
  else {
    cache->node_head = node;
    cache->node_list = node;
  }

  return node->value;
}

typedef enum ptm__line_type {
  PTMM__LINE_COMMENT,
  PTMM__LINE_PROPERTY,
  PTMM__LINE_BRUSH_FACE,
  PTMM__LINE_SCOPE_START,
  PTMM__LINE_SCOPE_END,
  PTMM__LINE_INVALID,
} ptm__line_type;

static ptm__line_type ptm__identify_line(const char** head) {
  switch (**head) {
    case '/': return PTMM__LINE_COMMENT;
    case '{': return PTMM__LINE_SCOPE_START;
    case '}': return PTMM__LINE_SCOPE_END;
    case '(': return PTMM__LINE_BRUSH_FACE;
    case '"': return PTMM__LINE_PROPERTY;
    default:  return PTMM__LINE_INVALID;
  }
}

typedef enum ptm__scope_type {
  PTMM__SCOPE_MAP,
  PTMM__SCOPE_ENTITY,
  PTMM__SCOPE_BRUSH,
} ptm__scope_type;

typedef struct ptm__property_node {
  struct ptm__property_node* next;
  char* key;
  char* value;
}ptm__property_node;

typedef struct ptm__brush_face_node {
  struct ptm__brush_face_node* next;
  ptm_brush_face value;
}ptm__brush_face_node;

typedef struct ptm__brush_node {
  struct ptm__brush_node* next;
  ptm__brush_face_node* face_list;
  ptm__brush_face_node* face_list_head;
  int face_list_length;
}ptm__brush_node;

typedef struct ptm__entity_node {
  struct ptm__entity_node* next;

  ptm__brush_node* brush_list;
  ptm__brush_node* brush_list_head;
  int brush_list_length;

  ptm__property_node* property_list;
  ptm__property_node* property_list_head;
  int property_list_length;
}ptm__entity_node;

ptm_map* ptm_load_source(const char* source, int source_len) {
  const char* head = source;
  const char* end = source + source_len + 1;
  ptm__scope_type scope = PTMM__SCOPE_MAP;

  ptm__string_cache cache;
  cache.node_list = NULL;
  cache.node_head = NULL;
  cache.node_arena = ptm__create_arena(512);
  cache.value_arena = ptm__create_arena(512);

  ptm__entity_node* entity_list = NULL;
  ptm__entity_node* entity_list_head = NULL;
  int entity_list_length = 0;

  ptm__entity_node* scoped_entity = NULL;
  ptm__brush_node* scoped_brush = NULL;

  while (head < end) {
    // Leading whitespace does not affect the meaning of a line
    ptm__consume_whitespace(&head);

    switch (ptm__identify_line(&head)) {
      case PTMM__LINE_INVALID:
      case PTMM__LINE_COMMENT: {
        // Don't do anything with a comment or invalid line
        break;
      }

      // When we "increase" or start scope, we are adding a new
      // child object to the parent (e.g. new brush for an
      // entity, or new entity for a map).

      case PTMM__LINE_SCOPE_START: {
        PTM_ASSERT(scope != PTMM__SCOPE_BRUSH);

        if (scope == PTMM__SCOPE_MAP) {
          scope = PTMM__SCOPE_ENTITY;
          int size = sizeof(ptm__entity_node);
          scoped_entity = (ptm__entity_node*)PTM_MALLOC(size);
          ptm__zero_memory(scoped_entity, size);
        }
        else if (scope == PTMM__SCOPE_ENTITY) {
          scope = PTMM__SCOPE_BRUSH;
          int size = sizeof(ptm__brush_node);
          scoped_brush = (ptm__brush_node*)PTM_MALLOC(size);
          ptm__zero_memory(scoped_brush, size);
        }

        break;
      }

      // When we "decrease" or end scope, we finalize the addition
      // of a child object, and are certain it is fully 
      // initialized. This is useful for, say, merging
      // func_groups into worldspawn instead of as
      // a normal entity.

      case PTMM__LINE_SCOPE_END: {
        PTM_ASSERT(scope != PTMM__SCOPE_MAP);

        if (scope == PTMM__SCOPE_ENTITY) {
          PTM_ASSERT(scoped_entity != NULL);
          scope = PTMM__SCOPE_MAP;

          if (entity_list_head != NULL) {
            entity_list_head->next = scoped_entity;
            entity_list_head = scoped_entity;
          }
          else {
            // This is the first entity that is added:
            // initialize the linked list and its head
            entity_list_head = scoped_entity;
            entity_list = scoped_entity;
          }

          entity_list_length++;
          scoped_entity = NULL;
          // todo: this is where the func_group merging would go
        }
        else if (scope == PTMM__SCOPE_BRUSH) {
          PTM_ASSERT(scoped_entity != NULL);
          PTM_ASSERT(scoped_brush != NULL);
          scope = PTMM__SCOPE_ENTITY;

          if (scoped_entity->brush_list_head != NULL) {
            scoped_entity->brush_list_head->next = scoped_brush;
            scoped_entity->brush_list_head = scoped_brush;
          }
          else {
            // This is the first brush added to the scoped
            // entity: initialize the linked list and its head.
            scoped_entity->brush_list_head = scoped_brush;
            scoped_entity->brush_list = scoped_brush;
          }

          scoped_entity->brush_list_length++;
          scoped_brush = NULL;
        }
        break;
      }

      // Properties are the key-value pairs that define
      // gameplay data for the entities.

      case PTMM__LINE_PROPERTY: {
        PTM_ASSERT(scope == PTMM__SCOPE_ENTITY);

        ptm__consume_whitespace(&head);
        if (*(head + 1) == '_') {
          break;
        }

        // Add a new property to the scoped entity's list
        int s = sizeof(ptm__property_node);
        ptm__property_node* property = (ptm__property_node*)PTM_MALLOC(s);
        property->next = NULL;

        if (scoped_entity->property_list_head != NULL) {
          scoped_entity->property_list_head->next = property;
          scoped_entity->property_list_head = property;
        }
        else {
          scoped_entity->property_list_head = property;
          scoped_entity->property_list = property;
        }

        scoped_entity->property_list_length++;
        
        // Read the key-value pair of strings into the new property
        property->key = ptm__consume_string(&head, '\"', &cache);
        property->value = ptm__consume_string(&head, '\"', &cache);
        break;
      }

      // Brush faces are the complex numeric lines that define
      // every mesh-related and visible property of an entity.
      
      case PTMM__LINE_BRUSH_FACE: {
        PTM_ASSERT(scope == PTMM__SCOPE_BRUSH);
        PTM_ASSERT(scoped_brush != NULL);

        // Add a new brush face to the current brush's list
        int s = sizeof(ptm__brush_face_node);
        ptm__brush_face_node* face = (ptm__brush_face_node*)PTM_MALLOC(s);
        face->next = NULL;

        if (scoped_brush->face_list_head != NULL) {
          scoped_brush->face_list_head->next = face;
          scoped_brush->face_list_head = face;
        }
        else {
          scoped_brush->face_list_head = face;
          scoped_brush->face_list = face;
        }

        scoped_brush->face_list_length++;

        // The line has the following format:
        // (x1 y1 z1) (x2 y2 z2) (x3 y3 z3) TEXTURE_NAME rotation scaleX scaleY
    
        // We store the data from it into this face struct.
        ptm_brush_face* f = &face->value;

        // First, we parse the 3 triangle points that comprise our plane.
        PTM_REAL p[3][3];

        for (int i = 0; i < 3; i++) {
          ptm__consume_until_after(&head, '(');
          p[i][0] = ptm__consume_number(&head);
          p[i][1] = ptm__consume_number(&head);
          p[i][2] = ptm__consume_number(&head);
          ptm__consume_until_after(&head, ')');
        }

        // Calculate the normal and plane constant from the points
        float v[2][3];
        ptm__subtract_vec3(p[0], p[1], v[0]);
        ptm__subtract_vec3(p[0], p[2], v[1]);
        ptm__cross_vec3(v[0], v[1], f->plane_normal);
        f->plane_c = ptm__dot_vec3(f->plane_normal, p[0]);

        // Now, read the texture string name
        f->texture_name = ptm__consume_string(&head, ' ', &cache);

        // Next, 2 blocks of uv information.
        for (int i = 0; i < 2; i++) {
          ptm__consume_until_after(&head, '[');
          f->texture_uv[i][0] = ptm__consume_number(&head);
          f->texture_uv[i][1] = ptm__consume_number(&head);
          f->texture_uv[i][2] = ptm__consume_number(&head);
          f->texture_offset[i] = ptm__consume_number(&head);
          ptm__consume_until_after(&head, ']');
        }

        // Finally, some closing texture info
        f->texture_rotation = ptm__consume_number(&head);
        f->texture_scale[0] = ptm__consume_number(&head);
        f->texture_scale[1] = ptm__consume_number(&head);
        break;
      }
    } 

    // Finished parsing the current line: consume to 
    // the next newline or until we finish the file
    for (;;) {
      int reached_newline = *head == '\n';
      int reached_end = head >= end;

      if (reached_newline) {
        head++;
        break;
      }
      if (reached_end) {
        break;
      }
      else {
        head++;
      }
    }
  }

  // We finished parsing the entire file structure.
  // All of our data was stored in linked lists, because we
  // were not sure of the size beforehand.

  // However, our returned struct wants to store continuous
  // arrays of data that are tightly packed into memory
  // for efficient iteration and fast freeing: thus, we
  // compact things with an array allocator and release
  // the old linked lists.
  ptm__arena* arena = ptm__create_arena(source_len);

  // Allocate map storage
  int size = sizeof(ptm_entity) * entity_list_length;
  ptm_entity* entities = (ptm_entity*)ptm__arena_alloc(arena, size);
  int entity_count = 0;

  while (entity_list != NULL) {
    ptm_entity* entity = &entities[entity_count++];

    // Copy entities
    size = sizeof(ptm_brush) * entity_list->brush_list_length;
    entity->brushes = (ptm_brush*)ptm__arena_alloc(arena, size);
    ptm__brush_node* brush_list = entity_list->brush_list;

    while (brush_list != NULL) {
      // Copy brushes
      ptm_brush* brush = &entity->brushes[entity->brush_count++];
      size = sizeof(ptm_brush_face) * brush_list->face_list_length;
      brush->faces = (ptm_brush_face*)ptm__arena_alloc(arena, size);
      ptm__brush_face_node* face_list = brush_list->face_list;

      while (face_list != NULL) {
        // Copy faces
        brush->faces[brush->face_count++] = face_list->value;

        // Free old faces
        ptm__brush_face_node* next_face = face_list->next;
        PTM_FREE(face_list);
        face_list = next_face;
      }

      // Free old brushes
      ptm__brush_node* next_brush = brush_list->next;
      PTM_FREE(brush_list);
      brush_list = next_brush;
    }

    // Copy properties
    size = sizeof(char*) * entity_list->property_list_length;
    entity->property_keys = (char**)ptm__arena_alloc(arena, size);
    entity->property_values = (char**)ptm__arena_alloc(arena, size);
    ptm__property_node* property_list = entity_list->property_list;

    while (property_list != NULL) {
      entity->property_keys[entity->property_count] = property_list->key;
      entity->property_values[entity->property_count] = property_list->value;
      entity->property_count++;

      // Free old properties
      ptm__property_node* next_property = property_list->next;
      PTM_FREE(property_list);
      property_list = next_property;
    }

    // Free old entities
    ptm__entity_node* next_entity = entity_list->next;
    PTM_FREE(entity_list);
    entity_list = next_entity;
  }

  // Clean up old arenas that are no longer used
  ptm__free_arena(cache.node_arena);

  // Create and return final structure
  ptm_map* map = (ptm_map*)PTM_MALLOC(sizeof(ptm_map));
  map->entities = entities;
  map->entity_count = entity_count;
  map->_arena = arena;
  map->_string_arena = cache.value_arena;
  return map;
}

void ptm_free(ptm_map* map) {
  ptm__free_arena(map->_arena);
  ptm__free_arena(map->_string_arena);
  PTM_FREE(map);
}

#ifndef PTM_NO_STDIO
#include <stdio.h>
ptm_map* ptm_load(const char* file_path) {
  FILE* file; 
#if defined(_MSC_VER) && _MSC_VER >= 1400
  fopen_s(&file, file_path, "r");
#else
  file = fopen(file_path, "r");
#endif
  fseek(file, 0, SEEK_END);
  int source_len = ftell(file);
  fseek(file, 0, SEEK_SET);
  char* source = (char*)PTM_MALLOC(source_len);
  fread(source, 1, source_len, file);
  fclose(file);

  ptm_map* map = ptm_load_source(source, source_len);

  PTM_FREE(source);
  return map;
}
#endif

/* === END IMPLEMENTATION === */
#endif // PT_MAP_IMPLEMENTATION
