#ifndef PT_MAP_H
#define PT_MAP_H

#define PTM_REAL float
#define PTM_INDEX unsigned short
#define PTM_HASH unsigned int

typedef struct ptm_string {
  char* data;
  PTM_HASH hash;
} ptm_string;

typedef struct ptm_brush_face {
  struct ptm_brush_face* next;
  PTM_REAL plane_normal[3];
  PTM_REAL plane_c;
  ptm_string texture_name;
  PTM_REAL texture_uv[2][3];
  PTM_REAL texture_offset[2];
  PTM_REAL texture_scale[2];
} ptm_brush_face;

typedef struct ptm_brush {
  struct ptm_brush* next;
  ptm_brush_face* faces;
  int face_count;
} ptm_brush;

typedef struct ptm_property {
  struct ptm_property* next;
  ptm_string key;
  ptm_string value;
} ptm_property;

typedef struct ptm_mesh {
  struct ptm_mesh* next;
  PTM_REAL* vertex_positions;
  PTM_REAL* vertex_texcoords;
  PTM_REAL* vertex_normals;
  PTM_REAL* vertex_tangents;
  int vertex_count;
  PTM_INDEX* indices;
  int index_count;
  struct ptm_string texture_name;
} ptm_mesh;

typedef struct ptm_entity {
  struct ptm_entity* next;
  struct ptm_string class_name;
  struct ptm_property* properties;
  struct ptm_brush* brushes;
  struct ptm_mesh* meshes;
  int property_count;
  int brush_count;
  int mesh_count;
} ptm_entity;

typedef struct ptm_entity_class {
  struct ptm_entity_class* next;
  struct ptm_string name;
  struct ptm_entity* entities;
  int entity_count;
} ptm_entity_class;

typedef struct ptm_map {
  struct ptm_entity_class* entity_classes;
  int entity_class_count;
  struct ptm_entity world;
  void* arena;
} ptm_map;

#ifndef PTM_NO_STDIO
ptm_map* ptm_load(const char* file_path);
#endif 

ptm_map* ptm_load_source(const char* source, int source_length);
void ptm_free(ptm_map* map);

#endif // PT_MAP_H

#ifdef PT_MAP_IMPLEMENTATION

#include <stdint.h> // for ptrdiff_t with string parsing
#include <stddef.h> // for intptr_t, with string parsing

#ifndef PTM_ASSERT
#include <assert.h>
#define PTM_ASSERT(expr) assert(expr)
#endif

#ifndef PTM_APUSH
#include <stdlib.h>
#define PTM_ACREATE(capacity) ptm__arena_create(capacity)
#define PTM_APUSH(arena, bytes) ptm__arena_push(arena, bytes)
#define PTM_AFREE(arena) ptm__arena_free(arena)
#endif

#ifndef PTM_STRTOR
#include <stdlib.h>
#define PTM_STRTOR(start, end) ptm__strtor(start, end)
#endif

#ifndef PTM_CREATE_HASH
#define PTM_CREATE_HASH(data, size) ptm__create_hash_fnv32(data, size)
#endif 

typedef enum ptm_scope {
  PTM_SCOPE_MAP,
  PTM_SCOPE_ENTITY,
  PTM_SCOPE_BRUSH,
} ptm_scope;

typedef struct ptm_arena {
  char* data;
  int head;
  int capacity;
} ptm_arena;

typedef struct ptm_cached_string {
  struct ptm_cached_string* next;
  struct ptm_string content;
} ptm_cached_string;

// - Memory
static void* ptm__arena_create(int capacity);
static void ptm__arena_free(void* opaque_arena);
static void* ptm__arena_push(void* opaque_arena, int bytes);

// - Util
static PTM_REAL ptm__strtor(const char* start, const char** end);
static PTM_HASH ptm__create_hash_fnv32(const char* data, int size);
static void ptm__copy_memory(void* dest, const void* src, int bytes);
static void ptm__zero_memory(void* dest, int bytes);

// - Math
static void ptm__subtract_vec3(const float* a, const float* b, float* r);
static float ptm__dot_vec3(const float* a, const float* b);
static void ptm__cross_vec3(const float* a, const float* b, float* r);

// - Parsing 
static void ptm__consume_until_at(const char** head, char value);
static void ptm__consume_until_after(const char** head, char value);
static void ptm__consume_whitespace(const char** head);
static PTM_REAL ptm__consume_number(const char** head);
static ptm_string ptm__consume_string(const char** head, char delimiter, 
  ptm_cached_string** cache, void* arena);

// - Meshing
static ptm_mesh* ptm__create_meshes(ptm_brush* brushes, void* arena);

// === API ===

ptm_map* ptm_load_source(const char* source, int source_length) {
  // Cache some hashed strings that we frequently evaluate
  PTM_HASH hash_classname = PTM_CREATE_HASH("classname", 9);
  PTM_HASH hash_worldspawn = PTM_CREATE_HASH("worldspawn", 10);
  PTM_HASH hash_func_group = PTM_CREATE_HASH("func_group", 10);

  // Initialize the map structure that will be returned
  void* arena = PTM_ACREATE(source_length * 2);
  ptm_map* map = (ptm_map*)PTM_APUSH(arena, sizeof *map);
  ptm__zero_memory(map, sizeof *map);
  map->arena = arena;

  // Initialize state that isn't returned, but helps a lot while parsing
  ptm_cached_string* string_cache = NULL;
  ptm_entity* scoped_entity = NULL;
  ptm_brush* scoped_brush = NULL;
  ptm_scope scope = PTM_SCOPE_MAP;

  // Tracking for our current position in the source, and when to stop
  const char* head = source;
  const char* end = source + source_length + 1;

  while (head < end) {
    // Leading whitespace does not affect the meaning of a line
    ptm__consume_whitespace(&head);

    switch (*head) {
      case '{': {
        PTM_ASSERT(scope != PTM_SCOPE_BRUSH);

        // We need to begin processing a new entity/brush:
        // allocate some memory in the arena.
        if (scope == PTM_SCOPE_MAP) {
          scope = PTM_SCOPE_ENTITY;
          scoped_entity = (ptm_entity*)PTM_APUSH(arena, sizeof(ptm_entity));
          ptm__zero_memory(scoped_entity, sizeof(ptm_entity));
        }
        else if (scope == PTM_SCOPE_ENTITY) {
          scope = PTM_SCOPE_BRUSH;
          scoped_brush = (ptm_brush*)PTM_APUSH(arena, sizeof(ptm_brush));
          ptm__zero_memory(scoped_brush, sizeof(ptm_brush));
        }

        break;
      }
      case '}': {
        PTM_ASSERT(scope != PTM_SCOPE_MAP);

        // We have finished processing an entity: now we need to determine 
        // where it should be stored.
        if (scope == PTM_SCOPE_ENTITY) {
          PTM_ASSERT(scoped_entity != NULL);
	        PTM_ASSERT(scoped_entity->class_name.data != NULL);
          scope = PTM_SCOPE_MAP;
  
          ptm_string class_name = scoped_entity->class_name;
          int is_func_group = class_name.hash == hash_func_group;
          int is_worldspawn = class_name.hash == hash_worldspawn;
          int is_world_entity = is_func_group | is_worldspawn;
          
          // Merge special entity brushes into the singleton "world" entity
          if (is_world_entity && scoped_entity->brushes != NULL) {
            ptm_brush* brush = scoped_entity->brushes;

            while (brush->next != NULL) {
              brush = brush->next;
            }

            brush->next = map->world.brushes;
            map->world.brushes = scoped_entity->brushes;
            map->world.brush_count += scoped_entity->brush_count;
          }

          // "worldspawn" properties define the "world" entity properties
          if (class_name.hash == hash_worldspawn) {
            map->world.properties = scoped_entity->properties;
            map->world.property_count = scoped_entity->property_count;
          }

          if (!is_world_entity) {
            // Generate a mesh for the entity
            scoped_entity->meshes = ptm__create_meshes(scoped_entity->brushes, arena);

            // Try and find an existing class that matches
            ptm_entity_class* entity_class = map->entity_classes;
            
            while (entity_class != NULL) {
              if (entity_class->name.hash == class_name.hash) {
                break;
              }
              entity_class = entity_class->next;
            }

            // Create a new class if one doesn't exist
            if (entity_class == NULL) {
              entity_class = (ptm_entity_class*)PTM_APUSH(arena, sizeof *entity_class);
              ptm__zero_memory(entity_class, sizeof *entity_class);
              entity_class->name = class_name;

              entity_class->next = map->entity_classes;
              map->entity_classes = entity_class;
              map->entity_class_count++;
            }

            // Add the scoped entity to it's class
            scoped_entity->next = entity_class->entities;
            entity_class->entities = scoped_entity;
            entity_class->entity_count++;
          }

          scoped_entity = NULL;
        }
        else if (scope == PTM_SCOPE_BRUSH) {
          PTM_ASSERT(scoped_entity != NULL);
          PTM_ASSERT(scoped_brush != NULL);
          scope = PTM_SCOPE_ENTITY;
          
          // Adding a brush is much easier than an entity: no special cases, just add
          scoped_brush->next = scoped_entity->brushes;
          scoped_entity->brushes = scoped_brush;
          scoped_entity->brush_count++;
          
          scoped_brush = NULL;
        }
        break;
      }
      // Line format: "property_key" "property_value"
      case '"': {
        PTM_ASSERT(scope == PTM_SCOPE_ENTITY);
        PTM_ASSERT(scoped_entity != NULL);

        ptm__consume_whitespace(&head);

        // Ignore any names with the prefix "_tb"
        // (These are used internally by trenchbroom)
        if (*(head + 1) == '_' && *(head + 2) == 't' && *(head + 3) == 'b') {
          break;
        }

        // Add a new property to the scoped entity's list
        ptm_property* property = PTM_APUSH(arena, sizeof *property);
        property->key = ptm__consume_string(&head, '"', &string_cache, arena);
        property->value = ptm__consume_string(&head, '"', &string_cache, arena);

        // The "classname" property is special: it is stored separately
        // because it *must* be defined for every entity.
        if (property->key.hash == hash_classname) {
          scoped_entity->class_name = property->value;
        }
        else {
          // Non-classname properties get added to the scoped entity's list
          property->next = scoped_entity->properties;
          scoped_entity->properties = property;
          scoped_entity->property_count++;
        }

        break;
      }
      // Line format: (x1 y1 z1) (x2 y2 z2) (x3 y3 z3) TEXTURE_NAME [ ux uy uz offsetX ] [ vx vy vz offsetY ] rotation scaleX scaleY
      case '(': {
        PTM_ASSERT(scope == PTM_SCOPE_BRUSH);
        PTM_ASSERT(scoped_brush != NULL);

        // Add a new brush face to the current brush's list
        ptm_brush_face* face = PTM_APUSH(arena, sizeof *face);

        face->next = scoped_brush->faces;
        scoped_brush->faces = face;
        scoped_brush->face_count++;

        // First, we parse the 3 triangle points that comprise our plane.
        PTM_REAL p[3][3];

        for (int i = 0; i < 3; i++) {
          ptm__consume_until_after(&head, '(');
          // Quake uses z=up, and we probably want y=up so swap X and Y here
          p[i][0] = ptm__consume_number(&head);
          p[i][2] = ptm__consume_number(&head);
          p[i][1] = ptm__consume_number(&head);
          ptm__consume_until_after(&head, ')');
        }

        // Calculate the normal and plane constant from the points
        float v[2][3];
        ptm__subtract_vec3(p[0], p[1], v[0]);
        ptm__subtract_vec3(p[0], p[2], v[1]);
        ptm__cross_vec3(v[0], v[1], face->plane_normal);
        face->plane_c = ptm__dot_vec3(face->plane_normal, p[0]);

        // Now, read the texture string name
        face->texture_name = ptm__consume_string(&head, ' ', &string_cache, arena);

        // Next, 2 blocks of uv information.
        for (int i = 0; i < 2; i++) {
          ptm__consume_until_after(&head, '[');
          face->texture_uv[i][0] = ptm__consume_number(&head);
          face->texture_uv[i][1] = ptm__consume_number(&head);
          face->texture_uv[i][2] = ptm__consume_number(&head);
          face->texture_offset[i] = ptm__consume_number(&head);
          ptm__consume_until_after(&head, ']');
        }

        // The rotation value is actually unused in valve220,
        // so we totally ignore and skip it.
        ptm__consume_whitespace(&head);
        ptm__consume_until_at(&head, ' ');

        // Finally, some closing texture info
        face->texture_scale[0] = ptm__consume_number(&head);
        face->texture_scale[1] = ptm__consume_number(&head);

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

  // Only after everything is parsed is worldspawn stable:
  // now we can create the mesh for it
  map->world.meshes = ptm__create_meshes(map->world.brushes, arena);

  // We finished parsing the map: return final structure
  return map;
}

void ptm_free(ptm_map* map) {
  PTM_AFREE(map->arena);
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
  int source_length = ftell(file);
  fseek(file, 0, SEEK_SET);
  void* arena = PTM_ACREATE(source_length);
  char* source = (char*)PTM_APUSH(arena, source_length);
  source_length = fread(source, 1, source_length, file);
  fclose(file);
  ptm_map* map = ptm_load_source(source, source_length);
  PTM_AFREE(arena);
  return map;
}
#endif

// === MEMORY ===

static void* ptm__arena_create(int capacity) {
  ptm_arena* arena = (ptm_arena*)malloc(sizeof *arena);
  arena->data = (char*)malloc(capacity);
  arena->capacity = capacity;
  arena->head = 0;
  return arena;
}

static void* ptm__arena_push(void* opaque_arena, int bytes) {
  ptm_arena* arena = (ptm_arena*)opaque_arena;
  PTM_ASSERT(arena->head + bytes <= arena->capacity);
  void* result = arena->data + arena->head;
  arena->head += bytes;
  return result;
}

static void ptm__arena_free(void* opaque_arena) {
  ptm_arena* arena = (ptm_arena*)opaque_arena;
  free(arena->data);
  free(arena);
}

// === MATH ===

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

// === UTIL ===

static PTM_REAL ptm__strtor(const char* start, const char** end) {
  #if PTM_REAL == double
    return strtod(start, (char**)end);
  #elif PTM_REAL == float
    return strtof(start, (char**)end);
  #else
    #error Value of PTM_REAL has no default implementation.
  #endif
}

static PTM_HASH ptm__create_hash_fnv32(const char* data, int size) {
  unsigned int hash = 2166136261u;

  for (int i = 0; i < size; i++) {
    hash ^= *data++;
    hash *= 16777619u;
  }

  return hash;
}

static void ptm__copy_memory(void* dest, const void* src, int bytes) {
  char* d = (char*)dest;
  const char* s = (const char*)src;

  for (int i = 0; i < bytes; i++)
    d[i] = s[i];
}

static void ptm__zero_memory(void* dest, int bytes) {
  char* d = (char*)dest;

  for (int i = 0; i < bytes; i++)
    d[i] = 0;
}

// === PARSING ===

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

static ptm_string ptm__consume_string(const char** head, char delimiter, ptm_cached_string** cache, void* arena) {
  // Parse a string between delimiters at head
  ptm__consume_until_after(head, delimiter);
  const char* start = *head;
  ptm__consume_until_at(head, delimiter);
  const char* end = *head;
  (*head)++;
  int length = (intptr_t)end - (intptr_t)start;
  PTM_HASH hash = PTM_CREATE_HASH(start, length);

  // Check for a cache hit as an early-out
  ptm_cached_string* string = *cache;

  while (string != NULL) {
    if (string->content.hash == hash) {
      return string->content;
    }
    string = string->next;
  }

  // Allocate new string from parsed value
  char* data = (char*)PTM_APUSH(arena, length + 1);
  ptm__copy_memory(data, start, length);
  data[length] = '\0';

  // Add new string to cache
  ptm_cached_string* new_string;
  new_string = (ptm_cached_string*)PTM_APUSH(arena, sizeof *new_string);
  new_string->content.hash = hash;
  new_string->content.data = data;
  new_string->next = *cache;
  *cache = new_string;

  return new_string->content;
}

// === MESHING ===

static ptm_mesh* ptm__create_meshes(ptm_brush* brushes, void* arena) {
  return NULL;
}

#endif // PT_MAP_IMPLEMENTATION
