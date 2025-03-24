/* 
    STB-style parser for `.map` files.
    Tested with Trenchbroom, could work with other level editors.

    Do this:
      #define PTB_MAP_IMPL
    before you include this file in *one* C or C++ file to create the 
    implementation.

    TODO:
      Merge `func_group` into worldspawn on scope decrease

      Better memory allocation - internal arena allocator?
      ^ Along with this, correctly freeing everything

      Consider simplifying parser functions: I think we
        can make due with simply incrementing a typed pointer?

      Usage code in this documentation

      License/author info in this documentation

      Feature parity with that cool really old repository
        (CSG union, meshing, texture uv calculation)

      Other cool map-related things (bsps, collision hulls, vis/portals, quake movement?)

      Another STB-style library for WAD loading would be cool!
        Or if we embed it into this for one an all-in-one quick 3d prototype tool

    REFERENCE:
      https://book.leveldesignbook.com/appendix/resources/formats/map
      https://github.com/stefanha/map-files

    OPTIONS:
      #define PTB_ASSERT to replace <assert.h>
      #define PTB_MALLOC/FREE/REALLOC to replace heap allocator
      #define PTB_NO_STDIO to remove <stdio.h> and ptb_load_map()
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
 */

#ifndef PTB_MAP_H
#define PTB_MAP_H

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

#ifdef PTB_MAP_IMPL

#ifndef PTB_ASSERT
#include <assert.h>
#define PTB_ASSERT(expr) assert(expr)
#endif

#ifndef PTB_MALLOC
#include <stdlib.h>
#define PTB_MALLOC(bytes) malloc(bytes)
#define PTB_FREE(buffer) free(buffer)
#define PTB_REALLOC(buffer, bytes) realloc(buffer, bytes)
#endif

#define PTB_LIST_RESIZE(list, count, type) \
  list = list == NULL \
    ? (type*)PTB_MALLOC(count * sizeof(type)) \
    : (type*)PTB_REALLOC(list, count * sizeof(type));

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
  ptb__arena_chunk* chunk = (ptb__arena_chunk*)PTB_MALLOC(sizeof(ptb__arena_chunk));
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
  ptb_map* map;
  const char* source;
  int length;
  int head;
  ptbm__scope_type scope;
} ptbm__parser;

static int ptbm__is_valid(const ptbm__parser* parser) {
  return parser->head < parser->length;
}

static char ptbm__peek_char(const ptbm__parser* parser, int offset) {
  return parser->source[parser->head + offset];
}

static ptb_entity* ptbm__current_entity(ptbm__parser* parser) {
  PTB_ASSERT(parser->map->entity_count > 0);
  PTB_ASSERT(parser->map->entities != NULL);

  return &parser->map->entities[parser->map->entity_count - 1];
}

static ptb_brush* ptbm__current_brush(ptbm__parser* parser) {
  ptb_entity* entity = ptbm__current_entity(parser);

  PTB_ASSERT(entity->brush_count > 0);
  PTB_ASSERT(entity->brushes != NULL);

  return &entity->brushes[entity->brush_count - 1];
}

static char ptbm__consume_char(ptbm__parser* parser) {
  char result = parser->source[parser->head];
  if (ptbm__is_valid(parser)) parser->head++;
  return result;
}

static void ptbm__consume_until_exclusive(ptbm__parser* parser, char value) {
  while (ptbm__is_valid(parser)) {
    // Consume everything up to (and excluding) the desired character
    if (ptbm__peek_char(parser, 0) != value) ptbm__consume_char(parser);
    else break;
  }
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
  ptbm__consume_whitespace(parser);
  char* end;
  const char* start = parser->source + parser->head;
  PTB_REAL result = strtod(start, &end);
  int distance = end - start;
  parser->head += distance;
  return 0;
}

static char* ptbm__consume_string(ptbm__parser* parser, char value) {
  // Skip everything until we find the first delimiter
  ptbm__consume_until_inclusive(parser, value);

  char* string = NULL;
  int string_len = 0;

  while (ptbm__is_valid(parser)) {
    // Consume until we reach the second delimiter
    char current_char = ptbm__consume_char(parser);

    if (current_char != value) {
      string_len++;
      PTB_LIST_RESIZE(string, string_len, char);
      string[string_len - 1] = current_char;
    }
    else break;
  }

  PTB_LIST_RESIZE(string, string_len + 1, char);
  string[string_len] = '\0';
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

ptb_map* ptb_load_map_source(const char* source, int source_len) {
  ptb__arena* arena = (ptb__arena*)PTB_MALLOC(sizeof(ptb__arena));
  ptb__init_arena(arena, source_len);

  ptb_map* map = (ptb_map*)PTB_MALLOC(sizeof(ptb_map));
  map->entities = NULL;
  map->entity_count = 0;
  map->_arena = arena;

  ptbm__parser ctx;
  ctx.scope = PTBM__SCOPE_MAP;
  ctx.head = 0;
  ctx.source = source;
  ctx.length = source_len;
  ctx.map = map;

  while (ptbm__is_valid(&ctx)) {
    // Leading whitespace does not affect the meaning of a line
    ptbm__consume_whitespace(&ctx);

    switch (ptbm__identify_line(&ctx)) {
      case PTBM__LINE_COMMENT: {
        // Don't do anything with a comment line
        break;
      }

      // When we "increase" or start scope, we are adding a new
      // child object to the parent (e.g. new brush for an
      // entity, or new entity for a map).

      case PTBM__LINE_SCOPE_START: {
        PTB_ASSERT(ctx.scope != PTBM__SCOPE_BRUSH);

        if (ctx.scope == PTBM__SCOPE_MAP) {
          ctx.scope = PTBM__SCOPE_ENTITY;

          // We are processing a new entity: append it to the map
          map->entity_count++;
          PTB_LIST_RESIZE(map->entities, map->entity_count, ptb_entity);
          ptb__zero_memory(ptbm__current_entity(&ctx), sizeof(ptb_entity));
        }
        else if (ctx.scope == PTBM__SCOPE_ENTITY) {
          ctx.scope = PTBM__SCOPE_BRUSH;

          // We are processing a new brush: append it to the entity
          ptb_entity* entity = ptbm__current_entity(&ctx);
          entity->brush_count++;
          PTB_LIST_RESIZE(entity->brushes, entity->brush_count, ptb_brush);
          ptb__zero_memory(ptbm__current_brush(&ctx), sizeof(ptb_brush));
        }
        break;
      }

      // When we "decrease" or end scope, we finalize the addition
      // of a child object, and are certain it is fully 
      // initialized. This is useful for, say, merging
      // func_groups into worldspawn instead of as
      // a normal entity.

      case PTBM__LINE_SCOPE_END: {
        PTB_ASSERT(ctx.scope != PTBM__SCOPE_MAP);

        if (ctx.scope == PTBM__SCOPE_ENTITY) {
          // todo: this is where the func_group merging would go
          ctx.scope = PTBM__SCOPE_MAP;
        }
        else if (ctx.scope == PTBM__SCOPE_BRUSH) {
          ctx.scope = PTBM__SCOPE_ENTITY;
        }
        break;
      }

      // Properties are the key-value pairs that define
      // gameplay data for the entities.

      case PTBM__LINE_PROPERTY: {
        PTB_ASSERT(ctx.scope == PTBM__SCOPE_ENTITY);

        ptb_entity* entity = ptbm__current_entity(&ctx);
        entity->property_count++;
        int new_count = entity->property_count;
        PTB_LIST_RESIZE(entity->property_keys, new_count, char*);
        PTB_LIST_RESIZE(entity->property_values, new_count, char*);

        // Read entity properties into current entity
        int last = entity->property_count - 1;
        entity->property_keys[last] = ptbm__consume_string(&ctx, '\"');
        entity->property_values[last] = ptbm__consume_string(&ctx, '\"');
        break;
      }

      // Brush faces are the complex numeric lines that define
      // every mesh-related and visible property of an entity.
      
      // (x1 y1 z1) (x2 y2 z2) (x3 y3 z3) TEXTURE_NAME rotation scaleX scaleY

      case PTBM__LINE_BRUSH_FACE: {
        PTB_ASSERT(ctx.scope == PTBM__SCOPE_BRUSH);

        ptb_brush* brush = ptbm__current_brush(&ctx);
        brush->face_count++;
        int new_count = brush->face_count;
        PTB_LIST_RESIZE(brush->faces, new_count, ptb_brush_face);
        ptb_brush_face* face = &brush->faces[brush->face_count - 1];

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
        ptb__cross_vec3(v[0], v[1], face->plane_normal);
        face->plane_c = ptb__dot_vec3(face->plane_normal, p[0]);

        // Now, read the texture string name
        face->texture_name = ptbm__consume_string(&ctx, ' ');

        // Next, 2 blocks of uv information.
        for (int i = 0; i < 2; i++) {
          ptbm__consume_until_inclusive(&ctx, '[');
          face->texture_uv[i][0] = ptbm__consume_number(&ctx);
          face->texture_uv[i][1] = ptbm__consume_number(&ctx);
          face->texture_uv[i][2] = ptbm__consume_number(&ctx);
          face->texture_offset[i] = ptbm__consume_number(&ctx);
          ptbm__consume_until_inclusive(&ctx, ']');
        }

        // Finally, some closing texture info
        face->texture_rotation = ptbm__consume_number(&ctx);
        face->texture_scale[0] = ptbm__consume_number(&ctx);
        face->texture_scale[1] = ptbm__consume_number(&ctx);
        break;
      }
    } 

    // Finished parsing the current line: while loop
    // continues until we run out of characters to process
    ptbm__consume_until_inclusive(&ctx, '\n');
  }

  return map;
}

void ptb_free_map(ptb_map* map) {
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

#endif // PTB_MAP_IMPL
#endif // PTB_MAP_H
