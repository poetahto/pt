/* 
    ptm_map - v0.01 - public domain `.map` loader
                            No warranty implied; use at your own risk

    Do this:
      #define PT_MAP_IMPLEMENTATION
    before you include this file in *one* C or C++ file to 
    create the implementation.

    BASIC USAGE:
      See ptm_demo.c for example.

    OPTIONS:
      #define PTM_ASSERT(expr)
        change how assertions are enforced

      #define PTM_ARENA_CREATE/ALLOC/FREE
        change arena allocation strategy

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
            
    TODO:
      make more stuff live in arenas instead of linked lists 
      (or move linked lists into arenas) for better performance, easier freeing

      coordinate system conversion (weird Z=up and stuff)
      (wait for scaling though, b/c float vs int precision for processing)
      
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

#define PTM_REAL float
#define PTM_HASH unsigned int

typedef struct ptm_string {
  char* value;
  PTM_HASH hash;
} ptm_string;

typedef struct ptm_texture {
  void* data;
  PTM_HASH hash;
} ptm_texture;

typedef struct ptm_brush_face {
  PTM_REAL plane_normal[3];
  PTM_REAL plane_c;
  ptm_string texture_name;
  PTM_REAL texture_uv[2][3];
  PTM_REAL texture_offset[2];
  PTM_REAL texture_scale[2];
} ptm_brush_face;

typedef struct ptm_brush {
  ptm_brush_face* faces;
  int face_count;
} ptm_brush;

typedef struct ptm_property {
  ptm_string key;
  ptm_string value;
} ptm_property;

typedef struct ptm_entity {
  PTM_LIST(property);
  PTM_LIST(brush);
} ptm_entity;

typedef struct ptm_class {
  ptm_string name;
  ptm_entity* entities;
  int entity_count;
} ptm_entity_class;

typedef struct ptm_map {
  ptm_class* classes;
  int class_count;
  ptm_entity world;
} ptm_map;

#ifndef PTM_NO_STDIO
void ptm_load(const char* file_path, ptm_map* map);
#endif 

void ptm_load_source(const char* source, int source_length, ptm_map* map);
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

#ifndef PTM_APUSH
#define PTM_ACREATE(capacity) ptm__arena_create(capacity)
#define PTM_APUSH(arena, bytes) ptm__arena_push(arena, bytes)
#define PTM_AFREE(arena) ptm__arena_free(arena)

#include <stdlib.h>

typedef struct ptm_arena {
  char* data;
  int head;
  int capacity;
} ptm_arena;

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

// Autogen boilerplate linked-list code
#define PTM_DECLARE_LIST(name)                                                  \
  typedef struct ptm_##name##_node {                                            \
    struct ptm_##name##_##node* next;                                           \
    ptm_##name## content;                                                       \
  };                                                                            \
                                                                                \
  typedef struct ptm_##name##_list {                                            \
    ptm_##name##_node* data;                                                    \
    ptm_##name##_node* head;                                                    \
    int count;                                                                  \
  };                                                                            \
                                                                                \
  ptm_##name##_node* ptm__push_##name##(ptm_##name##_list* list, void* arena) { \
    ptm_##name##_node* node;                                                    \
    node = (ptm_##name##_node*)PTM_APUSH(arena, sizeof *node);                  \
    *node = {0};                                                                \
                                                                                \
    if (list-count > 0) {                                                       \
      list->head->next = node;                                                  \
      list->head = node;                                                        \
    else {                                                                      \
      list->head = node;                                                        \
      list->data = node;                                                        \
    }                                                                           \
                                                                                \
    list->count++;                                                              \
    return node;                                                                \
  }

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

PTM_DECLARE_LIST(string)

typedef struct ptm_string_cache {
  ptm_string_list list; 
  void* string_arena;
  void* node_arena;
} ptm_string_cache;

static ptm_string ptm__consume_string(const char** head, char delimiter, ptm_string_cache* cache) 
{
  // Parse a string between delimiters at head
  ptm__consume_until_after(head, delimiter);
  const char* start = *head;
  ptm__consume_until_at(head, delimiter);
  const char* end = *head;
  (*head)++;
  int length = (intptr_t)end - (intptr_t)start;
  PTM_HASH hash = PTM_CREATE_HASH(string, length);


  // Check for a cache hit as an early-out
  ptm_string_node* node = cache->list.data;

  while (node != NULL) {
    if (node->content.hash == hash) {
      return node->content;
      break;
    }
    node = node->next;
  }

  // Allocate new string from parsed value
  char* value = (char*)PTM_APUSH(cache->string_arena, length + 1);
  ptm__copy_memory(value, start, length);
  value[length] = '\0';

  // Add new string to cache
  ptm_string_node* new_node = ptm__push_string(&cache->list, cache->node_arena);
  new_node->content.hash = hash;
  new_node->content.value = value;
  return new_node->content;
}

typedef enum ptm_line {
  PTM_LINE_COMMENT,
  PTM_LINE_PROPERTY,
  PTM_LINE_BRUSH_FACE,
  PTM_LINE_SCOPE_START,
  PTM_LINE_SCOPE_END,
  PTM_LINE_INVALID,
} ptm_line;

static ptm_line ptm__identify_line(const char** head) {
  switch (**head) {
    case '/': return PTM_LINE_COMMENT;
    case '{': return PTM_LINE_SCOPE_START;
    case '}': return PTM_LINE_SCOPE_END;
    case '(': return PTM_LINE_BRUSH_FACE;
    case '"': return PTM_LINE_PROPERTY;
    default:  return PTM_LINE_INVALID;
  }
}

typedef enum ptm_scope {
  PTM_SCOPE_MAP,
  PTM_SCOPE_ENTITY,
  PTM_SCOPE_BRUSH,
} ptm_scope;

void ptm_load_source(const char* source, int source_length, ptm_map* map) {
  const char* head = source;
  const char* end = source + source_length + 1;

  PTM_HASH class_key_hash = PTM_CREATE_HASH("classname");
  PTM_HASH world_class_hash = PTM_CREATE_HASH("worldspawn");
  PTM_HASH group_class_hash = PTM_CREATE_HASH("func_group");

  ptm_string_cache string_cache = {0};
  ptm_scope scope = PTM_SCOPE_MAP;
  ptm_entity scoped_entity;
  ptm_brush scoped_brush;

  // TODO: I have a feeling this loop is insecure/has
  // some overrun potential, given how unpredictably
  // we move the read head
  
  while (head < end) {
    // Leading whitespace does not affect the meaning of a line
    ptm__consume_whitespace(&head);

    switch (ptm__identify_line(&head)) {
      case PTM_LINE_INVALID:
      case PTM_LINE_COMMENT: {
        // Don't do anything with a comment or invalid line
        break;
      }

      // When we "increase" or start scope, we are adding a new
      // child object to the parent (e.g. new brush for an
      // entity, or new entity for a map).

      case PTM_LINE_SCOPE_START: {
        PTM_ASSERT(scope != PTM_SCOPE_BRUSH);

        if (scope == PTM_SCOPE_MAP) {
          scope = PTM_SCOPE_ENTITY;
	  scoped_entity = {0};
        }
        else if (scope == PTM_SCOPE_ENTITY) {
          scope = PTM_SCOPE_BRUSH;
	  scoped_brush = {0};
        }

        break;
      }

      // When we "decrease" or end scope, we finalize the addition
      // of a child object, and are certain it is fully 
      // initialized. This is useful for, say, merging
      // func_groups into worldspawn instead of as
      // a normal entity.

      case PTM_LINE_SCOPE_END: {
        PTM_ASSERT(scope != PTM_SCOPE_MAP);

        if (scope == PTM_SCOPE_ENTITY) {
          PTM_ASSERT(scoped_entity != NULL);
          scope = PTM_SCOPE_MAP;

	  // Find the class name for the scoped entity
	  ptm_string class_name = {0};

	  for (int i = 0; i < scoped_entity.property_count; i++) {
	    ptm_property* property = scoped_entity.properties[i];

 	    if (property->key.hash = class_key_hash) {
	      class_name = property->value;
	    }
	  }

	  PTM_ASSERT(class_name.value != NULL && 
			  "All entities must have a classname property");

	  bool is_world = class_name.hash == world_class_hash;
	  bool is_group = class_name.hash == group_class_hash;

          // worldspawn and func_groups are combined into one entity
	  if (is_world || is_group) {
	    PTM_LIST_COPY(scoped_entity.property, map->world.property);
	    PTM_LIST_COPY(scoped_entity.brush, map->world.brush);
	  }
	  else {
	    ptm_class* entity_class = NULL;
  
	    // Try and find an existing class that matches
	    for (int i = 0; i < map->class_count; i++) {
	      if (map->classes[i].name.hash == class_name.hash) {
	        entity_class = &map->classes[i];
	      }
	    }
  
	    // Create a new class if one doesn't exist
	    if (entity_class == NULL) {
	      ptm_class new_class = {0};
	      new_class.name = entity_class;
	      PTM_LIST_APPEND(map->class, new_class);
	      entity_class = &map->class_list[map->class_count - 1];
	    }

	    // Add our scoped entity to the class
	    PTM_LIST_APPEND(entity_class->entity, scoped_entity);
	  }
        }
        else if (scope == PTM_SCOPE_BRUSH) {
          PTM_ASSERT(scoped_entity != NULL);
          PTM_ASSERT(scoped_brush != NULL);
          scope = PTM_SCOPE_ENTITY;
	  PTM_LIST_APPEND(scoped_entity.brush, scoped_brush);
        }
        break;
      }

      // Properties are the key-value pairs that define
      // gameplay data for the entities.

      case PTM_LINE_PROPERTY: {
        PTM_ASSERT(scope == PTM_SCOPE_ENTITY);

        ptm__consume_whitespace(&head);

	// Ignore any names with the prefix "_tb"
	// (These are used internally by trenchbroom)
        if (*(head + 1) == '_' && *(head + 2) == 't' && *(head + 3) == 'b') {
          break;
        }

        // Add a new property to the scoped entity's list
	ptm_property property;
        ptm__consume_string(&head, '"', &property->key, &cache);
        ptm__consume_string(&head, '"', &property->value, &cache);
	PTM_LIST_APPEND(scoped_entity.property, property);
        break;
      }

      // Brush faces are the complex numeric lines that define
      // every mesh-related and visible property of an entity.
      
      case PTM_LINE_BRUSH_FACE: {
        PTM_ASSERT(scope == PTM_SCOPE_BRUSH);
        PTM_ASSERT(scoped_brush != NULL);

        // Add a new brush face to the current brush's list
	ptm_brush_face face;

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
        ptm__cross_vec3(v[0], v[1], face->plane_normal);
        face->plane_c = ptm__dot_vec3(face->plane_normal, p[0]);

        // Now, read the texture string name
	ptm_string texture_name;

        if (ptm__consume_string(&head, ' ', &texture_name, &cache)) {
	  
	}

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

	// Add to current brush
	PTM_LIST_APPEND(scoped_brush.brush_face, face);
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
  void* arena = PTM_ARENA_CREATE(source_length);

  // Allocate map storage
  int size = sizeof(ptm_entity) * entity_list_length;
  ptm_entity* entities = (ptm_entity*)PTM_ARENA_ALLOC(arena, size);
  int entity_count = 0;

  while (entity_list != NULL) {
    ptm_entity* entity = &entities[entity_count++];

    // Copy entities
    size = sizeof(ptm_brush) * entity_list->brush_list_length;
    entity->brushes = (ptm_brush*)PTM_ARENA_ALLOC(arena, size);
    ptm_brush_node* brush_list = entity_list->brush_list;

    while (brush_list != NULL) {
      // Copy brushes
      ptm_brush* brush = &entity->brushes[entity->brush_count++];
      size = sizeof(ptm_brush_face) * brush_list->face_list_length;
      brush->faces = (ptm_brush_face*)PTM_ARENA_ALLOC(arena, size);
      ptm_brush_face_node* face_list = brush_list->face_list;

      while (face_list != NULL) {
        // Copy faces
        brush->faces[brush->face_count++] = face_list->value;

        // Free old faces
        ptm_brush_face_node* next_face = face_list->next;
        PTM_FREE(face_list);
        face_list = next_face;
      }

      // Free old brushes
      ptm_brush_node* next_brush = brush_list->next;
      PTM_FREE(brush_list);
      brush_list = next_brush;
    }

    // Copy properties
    size = sizeof(char*) * entity_list->property_list_length;
    entity->property_keys = (char**)PTM_ARENA_ALLOC(arena, size);
    entity->property_values = (char**)PTM_ARENA_ALLOC(arena, size);
    ptm_property_node* property_list = entity_list->property_list;

    while (property_list != NULL) {
      entity->property_keys[entity->property_count] = property_list->key;
      entity->property_values[entity->property_count] = property_list->value;
      entity->property_count++;

      // Free old properties
      ptm_property_node* next_property = property_list->next;
      PTM_FREE(property_list);
      property_list = next_property;
    }

    // Free old entities
    ptm_entity_node* next_entity = entity_list->next;
    PTM_FREE(entity_list);
    entity_list = next_entity;
  }

  // Clean up old arenas that are no longer used
  PTM_ARENA_FREE(cache.node_arena);
 
  // populate final structure
  map->entities = entities;
  map->entity_count = entity_count;
  map->arena = arena;
}

void ptm_free(ptm_map* map) {
  PTM_ARENA_FREE(map->arena);
}

#ifndef PTM_NO_STDIO
#include <stdio.h>
void ptm_load(const char* file_path, ptm_map* map) {
  FILE* file; 
#if defined(_MSC_VER) && _MSC_VER >= 1400
  fopen_s(&file, file_path, "r");
#else
  file = fopen(file_path, "r");
#endif
  fseek(file, 0, SEEK_END);
  int source_length = ftell(file);
  fseek(file, 0, SEEK_SET);
  void* arena = PTM_ARENA_CREATE(source_length);
  char* source = (char*)PTM_ARENA_ALLOC(arena, source_length);
  fread(source, 1, source_length, file);
  fclose(file);
  ptm_load_source(source, source_length, map);
  PTM_ARENA_FREE(arena);
}
#endif

/* === END IMPLEMENTATION === */
#endif // PT_MAP_IMPLEMENTATION
