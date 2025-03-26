#include "pt_map.h"
#include <stdio.h>

static void print_map_info(const char* name, ptm_map* map);

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("USAGE: %s <map file>\n", argv[0]);
    return 1;
  }

  const char* map_name = argv[1];
  ptm_map* map = ptm_load(map_name);
  print_map_info(map_name, map);
  ptm_free(map);
  return 0;
}

static void print_map_info(const char* name, ptm_map* map) {
  // print general info
  printf("%s (%i entities)\n", name, map->entity_count);

  // print entity info
  for (int eid = 0; eid < map->entity_count; eid++) {
    ptb_entity* entity = &map->entities[eid];
    printf("  entity %i (%i brushes)\n", eid, entity->brush_count);

    // print properties
    for (int pid = 0; pid < entity->property_count; pid++) {
      char* key = entity->property_keys[pid];
      char* value = entity->property_values[pid];
      printf("    \"%s\" : \"%s\"\n", key, value);
    }
  }
}

#define PT_MAP_IMPLEMENTATION
#include "pt_map.h"
