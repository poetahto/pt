#pragma once

#include "tb_map.hpp"

struct tb_mesh {
  string_handle texture_string;

  int vertex_count;
  float *vertex_positions;
  float *vertex_uvs;
  float *vertex_normals;
  float *vertex_tangents;

  int index_count;
  unsigned short *indices;
};

struct tb_model {
  int mesh_count;
  tb_mesh *meshes;
};

void tb_model_create(const tb_entity *entity, tb_model *model);
void tb_model_free(tb_model *model);
