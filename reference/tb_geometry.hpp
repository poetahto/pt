#pragma once

#include "tb_map.hpp"

struct tb_geometry_vertex {
  float position[3];
};

struct tb_geometry_edge {
  int vertex_indices[2];
  int face_indices[2];
};

struct tb_geometry_face {
  int *edge_indices;
  int edge_count;
  string_handle texture;
  float normal_vector[3];
  float tangent_vector[3];
  float world_to_uv_matrix[4][4];
  float uv_scale[2];
  float uv_offset[2];
};

struct tb_geometry {
  tb_geometry_vertex *vertices;
  int vertex_count;

  tb_geometry_edge *edges;
  int edge_count;

  tb_geometry_face *faces;
  int face_count;
};

void tb_geometry_create(const tb_brush *brush, tb_geometry *geometry);
void tb_geometry_free(tb_geometry *geometry);