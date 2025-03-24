/* 
    STB-style parser for `.map` files.
    Tested with Trenchbroom, could work with other level editors.

    Do this:
      #define PTB_MAP_IMPL
    before you include this file in *one* C or C++ file to create the 
    implementation.

    TODO:
      Merge `func_group` into worldspawn on scope decrease

      Consider simplifying parser functions: I think we
        can make due with simply incrementing a typed pointer?

      Usage code in this documentation

      License/author info in this documentation

      Feature parity with that cool really old repository
        (CSG union, meshing, texture uv calculation)

      Other cool map-related things (bsps, collision hulls, vis/portals, quake movement?)

      Another STB-style library for WAD loading would be cool!
        Or if we embed it into this for one an all-in-one quick 3d prototype tool

    QUICK NOTES:
      The parser is bottlenecked heavily by the "strtod" function (almost
      everything inside a map file is a number).
      If performance is an issue, consider re-defining PTB_STRTOD to a
      faster implementation.

    OPTIONS:
      #define PTB_ASSERT to replace <assert.h>
      #define PTB_MALLOC/FREE/REALLOC to replace heap allocator
      #define PTB_STRTOD to replace number parsing
      #define PTB_REAL to change precision for floating points

    BACKGROUND:
      ".map" files define brush-based levels for games in a simple, 
      plaintext format. They were originally used in the first quake 
      game, and later modified for its descendants (half-life, ect.)

      While brush-based level design has fallen out of favor in mainstream 
      level design, it remains a powerful prototyping and blockout tool, 
      along with capturing a retro aesthetic.

      A map file is structured to not just define the visual layout of 
      a scene, but also the gameplay properties. Indeed, a map file is 
      simple a list of entities which may or may not be associated with 
      brush (think: lights, player spawn positions, ambient noise 
      emitters). These are all defined with key-value pairs which can be 
      interpreted by the engine at runtime.

    REFERENCE:
      https://book.leveldesignbook.com/appendix/resources/formats/map
      https://github.com/stefanha/map-files
 */

#ifndef PTB_MAP_H
#define PTB_MAP_H
/* === BEGIN HEADER === */

// Use this to change the precision for all floating-point values
#define PTB_REAL float

typedef struct ptb_brush_face {
  PTB_REAL plane_normal[3];
  PTB_REAL plane_c;
  const char* texture_name;
  PTB_REAL texture_uv[2][3];
  PTB_REAL texture_offset[2];
  PTB_REAL texture_scale[2];
  PTB_REAL texture_rotation;
} ptb_brush_face;

typedef struct ptb_brush {
  ptb_brush_face* faces;
  int face_count;
} ptb_brush;

typedef struct ptb_entity {
  char** property_keys;
  char** property_values;
  int property_count;
  ptb_brush* brushes;
  int brush_count;
} ptb_entity;

typedef struct ptb_map {
  ptb_entity* entities;
  int entity_count;

  struct ptb__arena* _arena;
} ptb_map;

#ifndef PTB_NO_STDIO
ptb_map* ptb_load_map(const char* file_path);
#endif 

ptb_map* ptb_load_map_source(const char* source, int source_len);
void ptb_free_map(ptb_map* map);

/* === END HEADER === */
#endif // PTB_MAP_H

#ifdef PTB_MAP_IMPL
/* === BEGIN IMPLEMENTATION === */

#ifndef PTB_ASSERT
#include <assert.h>
#define PTB_ASSERT(expr) assert(expr)
#endif

#ifndef PTB_STRTOD
#include <stdlib.h>
#define PTB_STRTOD strtod
#endif

#ifndef PTB_MALLOC
#include <stdlib.h>
#define PTB_MALLOC(bytes) malloc(bytes)
#define PTB_FREE(buffer) free(buffer)
#define PTB_REALLOC(buffer, bytes) realloc(buffer, bytes)
#endif

static void ptb__subtract_vec3(const float* a, const float* b, float* r) {
  r[0] = a[0] - b[0];
  r[1] = a[1] - b[1];
  r[2] = a[2] - b[2];
}

static float ptb__dot_vec3(const float* a, const float* b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void ptb__cross_vec3(const float* a, const float* b, float* r) {
  r[0] = a[1] * b[2] - a[2] * b[1];
  r[1] = a[2] * b[0] - a[0] * b[2];
  r[2] = a[0] * b[1] - a[1] * b[0];
}

static void ptb__zero_memory(void* memory, int bytes) {
  char* m = (char*)memory;

  for (int i = 0; i < bytes; i++) {
    m[i] = 0;
  }
}

typedef struct ptb__arena_chunk {
  struct ptb__arena_chunk* next;
  void* data;
  int head;
} ptb__arena_chunk;

typedef struct ptb__arena {
  ptb__arena_chunk* chunk_list;
  ptb__arena_chunk* current_chunk;
  int chunk_capacity;
} ptb__arena;

static void ptb__add_arena_chunk(ptb__arena* arena) {
  int size = sizeof(ptb__arena_chunk);
  ptb__arena_chunk* chunk = (ptb__arena_chunk*)PTB_MALLOC(size);
  chunk->data = PTB_MALLOC(arena->chunk_capacity);
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

static void ptb__init_arena(ptb__arena* arena, int chunk_capacity) {
  arena->chunk_capacity = chunk_capacity;
  arena->chunk_list = NULL;
  arena->current_chunk = NULL;
  ptb__add_arena_chunk(arena);
}

static void* ptb__arena_alloc(ptb__arena* arena, int bytes) {
  PTB_ASSERT(bytes < arena->chunk_capacity);

  if (arena->current_chunk->head + bytes > arena->chunk_capacity) {
    ptb__add_arena_chunk(arena);
  }

  void* result = ((char*)arena->current_chunk->data) + arena->current_chunk->head;
  arena->current_chunk->head += bytes;
  return result;
}

static void ptb__free_arena(ptb__arena* arena) {
  ptb__arena_chunk* chunk = arena->chunk_list;

  while (chunk != NULL) {
    PTB_FREE(chunk->data);
    chunk = chunk->next;
    PTB_FREE(chunk);
  }

  arena->current_chunk = NULL;
  arena->chunk_list = NULL;
}

typedef enum ptbm__line_type {
  PTBM__LINE_COMMENT,
  PTBM__LINE_PROPERTY,
  PTBM__LINE_BRUSH_FACE,
  PTBM__LINE_SCOPE_START,
  PTBM__LINE_SCOPE_END,
  PTBM__LINE_INVALID,
} ptbm__line_type;

typedef enum ptbm__scope_type {
  PTBM__SCOPE_MAP,
  PTBM__SCOPE_ENTITY,
  PTBM__SCOPE_BRUSH,
} ptbm__scope_type;

typedef struct ptbm__parser {
  const char* source;
  int length;
  int head;
} ptbm__parser;

static int ptbm__is_valid(const ptbm__parser* parser) {
  return parser->head < parser->length;
}

static char ptbm__peek_char(const ptbm__parser* parser, int offset) {
  return parser->source[parser->head + offset];
}

static char ptbm__consume_char(ptbm__parser* parser) {
  char result = parser->source[parser->head];
  if (ptbm__is_valid(parser)) parser->head++;
  return result;
}

static void ptbm__consume_until_inclusive(ptbm__parser* parser, char value) {
  while (ptbm__is_valid(parser)) {
    // Consume everything up to (and including) the desired character
    if (ptbm__consume_char(parser) == value) break;
  }
}

static void ptbm__consume_whitespace(ptbm__parser* parser) {
  while (ptbm__is_valid(parser)) {
    char value = ptbm__peek_char(parser, 0);

    int is_space = value == ' ';
    int is_tab = value == '\t';

    if (is_space || is_tab) {
      ptbm__consume_char(parser);
    } 
    else {
      break;
    } 
  }
}

static PTB_REAL ptbm__consume_number(ptbm__parser* parser) {
  char* end;
  const char* start = parser->source + parser->head;
  PTB_REAL result = PTB_STRTOD(start, &end);
  int distance = end - start;
  parser->head += distance;
  return result;
}

static char* ptbm__consume_string(ptbm__parser* parser, char value) {
  // Skip everything until we find the first delimiter
  ptbm__consume_until_inclusive(parser, value);

  char* string = (char*)PTB_MALLOC(8);
  int string_length = 0;
  int string_capacity = 8;

  while (ptbm__is_valid(parser)) {
    // Consume until we reach the second delimiter
    char current_char = ptbm__consume_char(parser);

    if (current_char != value) {
      // Check to see if string buffer needs to grow
      if (string_length + 1 >= string_capacity) {
        string_capacity *= 2;
        string = (char*)PTB_REALLOC(string, string_capacity);
      }

      string_length++;
      string[string_length - 1] = current_char;
    }
    else break;
  }


  // Potentially expand and add a null terminator
  if (string_length + 1 >= string_capacity) {
    string_capacity++;
    string = (char*)PTB_REALLOC(string, string_capacity);
  }
  string[string_length] = '\0';

  return string;
}

static ptbm__line_type ptbm__identify_line(const ptbm__parser* parser) {
  switch (ptbm__peek_char(parser, 0)) {
    case '/': return PTBM__LINE_COMMENT;
    case '{': return PTBM__LINE_SCOPE_START;
    case '}': return PTBM__LINE_SCOPE_END;
    case '(': return PTBM__LINE_BRUSH_FACE;
    case '"': return PTBM__LINE_PROPERTY;
    default:  return PTBM__LINE_INVALID;
  }
}

typedef struct ptbm__property_node {
  struct ptbm__property_node* next;
  char* key;
  char* value;
}ptbm__property_node;

typedef struct ptbm__brush_face_node {
  struct ptbm__brush_face_node* next;
  ptb_brush_face value;
}ptbm__brush_face_node;

typedef struct ptbm__brush_node {
  struct ptbm__brush_node* next;
  ptbm__brush_face_node* face_list;
  ptbm__brush_face_node* face_list_head;
  int face_list_length;
}ptbm__brush_node;

typedef struct ptbm__entity_node {
  struct ptbm__entity_node* next;

  ptbm__brush_node* brush_list;
  ptbm__brush_node* brush_list_head;
  int brush_list_length;

  ptbm__property_node* property_list;
  ptbm__property_node* property_list_head;
  int property_list_length;
}ptbm__entity_node;

ptb_map* ptb_load_map_source(const char* source, int source_len) {
  ptbm__parser ctx;
  ctx.head = 0;
  ctx.source = source;
  ctx.length = source_len;

  ptbm__scope_type scope = PTBM__SCOPE_MAP;

  ptbm__entity_node* entity_list = NULL;
  ptbm__entity_node* entity_list_head = NULL;
  int entity_list_length = 0;

  ptbm__entity_node* scoped_entity = NULL;
  ptbm__brush_node* scoped_brush = NULL;

  while (ptbm__is_valid(&ctx)) {
    // Leading whitespace does not affect the meaning of a line
    ptbm__consume_whitespace(&ctx);

    switch (ptbm__identify_line(&ctx)) {
      case PTBM__LINE_INVALID:
      case PTBM__LINE_COMMENT: {
        // Don't do anything with a comment or invalid line
        break;
      }

      // When we "increase" or start scope, we are adding a new
      // child object to the parent (e.g. new brush for an
      // entity, or new entity for a map).

      case PTBM__LINE_SCOPE_START: {
        PTB_ASSERT(scope != PTBM__SCOPE_BRUSH);

        if (scope == PTBM__SCOPE_MAP) {
          scope = PTBM__SCOPE_ENTITY;
          int size = sizeof(ptbm__entity_node);
          scoped_entity = (ptbm__entity_node*)PTB_MALLOC(size);
          ptb__zero_memory(scoped_entity, size);
        }
        else if (scope == PTBM__SCOPE_ENTITY) {
          scope = PTBM__SCOPE_BRUSH;
          int size = sizeof(ptbm__brush_node);
          scoped_brush = (ptbm__brush_node*)PTB_MALLOC(size);
          ptb__zero_memory(scoped_brush, size);
        }

        break;
      }

      // When we "decrease" or end scope, we finalize the addition
      // of a child object, and are certain it is fully 
      // initialized. This is useful for, say, merging
      // func_groups into worldspawn instead of as
      // a normal entity.

      case PTBM__LINE_SCOPE_END: {
        PTB_ASSERT(scope != PTBM__SCOPE_MAP);

        if (scope == PTBM__SCOPE_ENTITY) {
          PTB_ASSERT(scoped_entity != NULL);
          scope = PTBM__SCOPE_MAP;

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
        else if (scope == PTBM__SCOPE_BRUSH) {
          PTB_ASSERT(scoped_entity != NULL);
          PTB_ASSERT(scoped_brush != NULL);
          scope = PTBM__SCOPE_ENTITY;

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

      case PTBM__LINE_PROPERTY: {
        PTB_ASSERT(scope == PTBM__SCOPE_ENTITY);

        // Add a new property to the scoped entity's list
        int s = sizeof(ptbm__property_node);
        ptbm__property_node* property = (ptbm__property_node*)PTB_MALLOC(s);
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
        property->key = ptbm__consume_string(&ctx, '\"');
        property->value = ptbm__consume_string(&ctx, '\"');
        break;
      }

      // Brush faces are the complex numeric lines that define
      // every mesh-related and visible property of an entity.
      
      case PTBM__LINE_BRUSH_FACE: {
        PTB_ASSERT(scope == PTBM__SCOPE_BRUSH);
        PTB_ASSERT(scoped_brush != NULL);

        // Add a new brush face to the current brush's list
        int s = sizeof(ptbm__brush_face_node);
        ptbm__brush_face_node* face = (ptbm__brush_face_node*)PTB_MALLOC(s);
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
        ptb_brush_face* f = &face->value;

        // First, we parse the 3 triangle points that comprise our plane.
        PTB_REAL p[3][3];

        for (int i = 0; i < 3; i++) {
          ptbm__consume_until_inclusive(&ctx, '(');
          p[i][0] = ptbm__consume_number(&ctx);
          p[i][1] = ptbm__consume_number(&ctx);
          p[i][2] = ptbm__consume_number(&ctx);
          ptbm__consume_until_inclusive(&ctx, ')');
        }

        // Calculate the normal and plane constant from the points
        float v[2][3];
        ptb__subtract_vec3(p[0], p[1], v[0]);
        ptb__subtract_vec3(p[0], p[2], v[1]);
        ptb__cross_vec3(v[0], v[1], f->plane_normal);
        f->plane_c = ptb__dot_vec3(f->plane_normal, p[0]);

        // Now, read the texture string name
        f->texture_name = ptbm__consume_string(&ctx, ' ');

        // Next, 2 blocks of uv information.
        for (int i = 0; i < 2; i++) {
          ptbm__consume_until_inclusive(&ctx, '[');
          f->texture_uv[i][0] = ptbm__consume_number(&ctx);
          f->texture_uv[i][1] = ptbm__consume_number(&ctx);
          f->texture_uv[i][2] = ptbm__consume_number(&ctx);
          f->texture_offset[i] = ptbm__consume_number(&ctx);
          ptbm__consume_until_inclusive(&ctx, ']');
        }

        // Finally, some closing texture info
        f->texture_rotation = ptbm__consume_number(&ctx);
        f->texture_scale[0] = ptbm__consume_number(&ctx);
        f->texture_scale[1] = ptbm__consume_number(&ctx);
        break;
      }
    } 

    // Finished parsing the current line: while loop
    // continues until we run out of characters to process
    ptbm__consume_until_inclusive(&ctx, '\n');
  }

  // We finished parsing the entire file structure.
  // All of our data was stored in linked lists, because we
  // were not sure of the size beforehand.

  // However, our returned struct wants to store continuous
  // arrays of data that are tightly packed into memory
  // for efficient iteration and fast freeing: thus, we
  // compact things with an array allocator and release
  // the old linked lists.
  ptb__arena* arena = (ptb__arena*)PTB_MALLOC(sizeof(ptb__arena));
  ptb__init_arena(arena, source_len);

  // Allocate map storage
  int size = sizeof(ptb_entity) * entity_list_length;
  ptb_entity* entities = (ptb_entity*)ptb__arena_alloc(arena, size);
  int entity_count = 0;

  while (entity_list != NULL) {
    ptb_entity* entity = &entities[entity_count++];

    // Copy entities
    size = sizeof(ptb_brush) * entity_list->brush_list_length;
    entity->brushes = (ptb_brush*)ptb__arena_alloc(arena, size);
    ptbm__brush_node* brush_list = entity_list->brush_list;

    while (brush_list != NULL) {
      // Copy brushes
      ptb_brush* brush = &entity->brushes[entity->brush_count++];
      size = sizeof(ptb_brush_face) * brush_list->face_list_length;
      brush->faces = (ptb_brush_face*)ptb__arena_alloc(arena, size);
      ptbm__brush_face_node* face_list = brush_list->face_list;

      while (face_list != NULL) {
        // Copy faces
        brush->faces[brush->face_count++] = face_list->value;

        // Free old faces
        ptbm__brush_face_node* next_face = face_list->next;
        PTB_FREE(face_list);
        face_list = next_face;
      }

      // Free old brushes
      ptbm__brush_node* next_brush = brush_list->next;
      PTB_FREE(brush_list);
      brush_list = next_brush;
    }

    // Copy properties
    size = sizeof(char*) * entity_list->property_list_length;
    entity->property_keys = (char**)ptb__arena_alloc(arena, size);
    entity->property_values = (char**)ptb__arena_alloc(arena, size);
    ptbm__property_node* property_list = entity_list->property_list;

    while (property_list != NULL) {
      entity->property_keys[entity->property_count] = property_list->key;
      entity->property_values[entity->property_count] = property_list->value;
      entity->property_count++;

      // Free old properties
      ptbm__property_node* next_property = property_list->next;
      PTB_FREE(property_list);
      property_list = next_property;
    }

    // Free old entities
    ptbm__entity_node* next_entity = entity_list->next;
    PTB_FREE(entity_list);
    entity_list = next_entity;
  }

  // Create and return final structure
  ptb_map* map = (ptb_map*)PTB_MALLOC(sizeof(ptb_map));
  map->entities = entities;
  map->entity_count = entity_count;
  map->_arena = arena;
  return map;
}

void ptb_free_map(ptb_map* map) {
  ptb__free_arena(map->_arena);
  PTB_FREE(map->_arena);
  PTB_FREE(map);
}

#ifndef PTB_NO_STDIO
#include <stdio.h>
ptb_map* ptb_load_map(const char* file_path) {
  FILE* file; 
#if defined(_MSC_VER) && _MSC_VER >= 1400
  fopen_s(&file, file_path, "r");
#else
  file = fopen(file_path, "r");
#endif
  fseek(file, 0, SEEK_END);
  int source_len = ftell(file);
  fseek(file, 0, SEEK_SET);
  char* source = (char*)PTB_MALLOC(source_len);
  fread(source, 1, source_len, file);
  fclose(file);

  ptb_map* map = ptb_load_map_source(source, source_len);

  PTB_FREE(source);
  return map;
}
#endif

/* === END IMPLEMENTATION === */
#endif // PTB_MAP_IMPL
