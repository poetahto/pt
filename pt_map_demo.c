#define PT_MAP_IMPLEMENTATION
#include "pt_map.h"
#include <stdio.h>

static int count_brushes(ptm_brush* brush) {
  int count = 0;

  while (brush != NULL) {
    count++;
    brush = brush->next;
  }

  return count;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("USAGE: %s <map file>\n", argv[0]);
    return 1;
  }

  const char* map_file_name = argv[1];
  ptm_map* map = ptm_load(map_file_name);
  
  int brush_count = 0;
  int entity_class_count = 0;
  int entity_count = 0;

  // print world info
  int world_brush_count = count_brushes(map.world_brushes);
  printf("WORLDSPAWN: %i brushes\n", world_brush_count);
  brush_count += world_brush_count;
  ptm_property* world_property = map.world_properties;

  while (world_property != NULL) {
    printf("  \"%s\" \"%s\"\n", world_property->key.data, world_property->value.data);
    world_property = world_property->next;
  }

  // print entity info
  ptm_entity_class* entity_class = map.entity_classes;

  while (entity_class != NULL) {
    ptm_entity* entity = entity_class->entities;

    while (entity != NULL) {
      brush_count += count_brushes(entity->brushes);
      entity_count++;
      entity = entity->next;
    }

    printf("%s: %i entities\n", entity_class->name.data, entity_count);
    entity_class_count++;
    entity_class = entity_class->next;
  }

  printf("\n%s: %i brushes, %i classes, %i entities\n", map_file_name, brush_count, entity_class_count, entity_count);
  ptm_free(map);
  return 0;
}
