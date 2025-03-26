#define PT_MAP_IMPLEMENTATION
#include "pt_map.h"
#include <stdio.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("USAGE: %s <map file>\n", argv[0]);
    return 1;
  }

  const char* map_file_name = argv[1];
  ptm_map map;
  ptm_load(map_file_name, &map);
  int total_brushes = 0;

  // print worldspawn info
  printf("WORLDSPAWN: %i brushes\n", map.world.brush_count);
  total_brushes += map.world.brush_count;

  for (int i = 0; i < map.world.property_count; i++) {
    char* key = map.world.property_keys[i];
    char* value = map.world.property_values[i];
    printf("  \"%s\" \"%s\"\n", key, value);
  }

  // print class info
  for (int cid = 0; cid < map.class_count; cid++) {
    ptm_class* class = &map->classes[cid];
    printf("%s: %i entities\n", class->name, class->entity_count);

    for (int eid = 0; eid < class->entity_count; eid++) {
      ptm_entity* entity = &class->entities[eid];
      total_brushes += entity->brush_count;
    }
  }

  printf("\nparsed %s, %i total brushes\n", map_file_name, total_brushes);
  ptm_free(&map);
  return 0;
}
