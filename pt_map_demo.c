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
  
  int total_brush_count = 0;
  int total_entity_class_count = 0;
  int total_entity_count = 0;

  // print world info
  int world_brush_count = count_brushes(map->world_brushes);
  printf("worldspawn: %i brushes\n", world_brush_count);
  total_brush_count += world_brush_count;
  ptm_property* world_property = map->world_properties;

  while (world_property != NULL) {
    printf("  \"%s\" \"%s\"\n", world_property->key.data, world_property->value.data);
    world_property = world_property->next;
  }

  // print entity info
  ptm_entity_class* entity_class = map->entity_classes;

  while (entity_class != NULL) {
    ptm_entity* entity = entity_class->entities;
    int entity_count = 0;

    while (entity != NULL) {
      total_brush_count += count_brushes(entity->brushes);
      entity_count++;
      entity = entity->next;
    }

    printf("%s: %i entities\n", entity_class->name.data, entity_count);
    total_entity_count += entity_count;
    total_entity_class_count++;
    entity_class = entity_class->next;
  }

  printf("\n%s: %i brushes, %i classes, %i entities\n", map_file_name, total_brush_count, total_entity_class_count, total_entity_count);
  ptm_free(map);
  return 0;
}
