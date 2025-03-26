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

  // process map
  int brush_total = 0;
  int entity_total = map.entity_count;

  // print entities
  for (int eid = 0; eid < entity_total; eid++) {
    ptm_entity* entity = &map.entities[eid];
    brush_total += entity->brush_count;
    printf("  entity %i ", eid);

    // print brushes
    if (entity->brush_count > 0) {
      const char* plural = entity->brush_count > 1 ? "es" : "";
      printf("(%i brush%s)\n", entity->brush_count, plural);
    }
    else {
      printf("(point)\n");
    }

    // print properties
    for (int pid = 0; pid < entity->property_count; pid++) {
      char* key = entity->property_keys[pid];
      char* value = entity->property_values[pid];
      printf("    \"%s\" : \"%s\"\n", key, value);
    }
  }

  printf("\n%s: %i entities, %i brushes\n", 
          map_file_name, entity_total, brush_total);

  ptm_free(&map);
  return 0;
}
