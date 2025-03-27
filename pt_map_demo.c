#define PT_MAP_IMPLEMENTATION
#include "pt_map.h"
#include <stdio.h>
#include <string.h>

static int compare_entity_classes(const void* opaque_first, const void* opaque_second) {
  const ptm_entity_class* first = (const ptm_entity_class*)opaque_first;
  const ptm_entity_class* second = (const ptm_entity_class*)opaque_second;
  return strcmp(first->name.data, second->name.data);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("USAGE: %s <map file>\n", argv[0]);
    return 1;
  }

  const char* map_file_name = argv[1];
  ptm_map* map = ptm_load(map_file_name);

  int total_brush_count = 0;
  int total_entity_count = 0;

  // print world info
  printf("worldspawn: %i brushes\n\n", map->world_brush_count);
  total_brush_count += map->world_brush_count;

  // print entity info
  int count = map->entity_class_count;
  ptm_entity_class* list = malloc(sizeof(*list) * count);
  PTM_COPY_LIST(map->entity_classes, list, ptm_entity_class);
  qsort(list, count, sizeof(*list), compare_entity_classes);

  for (int i = 0; i < count; i++) {
    int brush_count = 0;
    ptm_entity* entity = list[i].entities;

    while (entity != NULL) {
      brush_count += entity->brush_count;
      entity = entity->next;
    }

    char* entity_name = list[i].entity_count != 1 ? "entities" : "entity";
    printf("%s: %i %s", list[i].name.data, list[i].entity_count, entity_name);

    if (brush_count > 0) {
      char* brush_name = brush_count != 1 ? "brushes" : "brush";
      printf(", %i %s\n", brush_count, brush_name);
    }
    else {
      putchar('\n');
    }

    total_entity_count += list[i].entity_count;
    total_brush_count += brush_count;
  }

  printf("\n%s: %i brushes, %i classes, %i entities\n", map_file_name, total_brush_count, map->entity_class_count, total_entity_count);
  ptm_free(map);
  return 0;
}
