#include "tb_model.hpp"
#include "tb_geometry.hpp"
#include <math.h>   // for roundf
#include <stdlib.h> // for malloc
#include <string.h> // for memcpy

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

template <typename T> struct list {
  T *data;
  int count;
  int capacity;
};

struct mesh_data {
  int vertex_count;
  list<float> vertex_positions;
  list<float> vertex_tangents;
  list<float> vertex_normals;
  list<float> vertex_uvs;
  list<unsigned short> indices;
  string_handle texture;
};

template <typename T> static list<T> list_create(int initial_capacity);
template <typename T> static void list_add(list<T> *list, T value);
template <typename T> static void remove_from_list(list<T> *list, T value);
static void vec3_add(float *out_result, const float *a, const float *b);
static void vec3_subtract(float *out_result, const float *a, const float *b);
static float vec3_dot_product(const float *a, const float *b);
static void vec3_cross_product(float *out_result, const float *a,
                               const float *b);
static float lerp(float a, float b, float t);
static float round_to_int(float value);
static void free_mesh(tb_mesh *mesh);
static void create_mesh_data(mesh_data *data, string_handle texture);

static void tb_model_create(const tb_geometry *g, int count, tb_model *model) {
  list<mesh_data> meshes = list_create<mesh_data>(10);

  for (int geo_index = 0; geo_index < count; geo_index++) {
    const tb_geometry *geometry = &g[geo_index];
    for (int face_index = 0; face_index < geometry->face_count; face_index++) {
      const tb_geometry_face *face = &geometry->faces[face_index];

      // cache or create a mesh that uses this face's texture
      mesh_data *mesh = nullptr;

      for (int mesh_index = 0; mesh_index < meshes.count; mesh_index++) {
        mesh_data *current_mesh = &meshes.data[mesh_index];
        if (current_mesh->texture == face->texture) {
          mesh = current_mesh;
          break;
        }
      }
      if (mesh == nullptr) {
        list_add(&meshes, {});
        mesh = &meshes.data[meshes.count - 1];
        create_mesh_data(mesh, face->texture);
      }

      // sort the edges into a continuous loop
      const tb_geometry_edge &initial_edge =
          geometry->edges[face->edge_indices[0]];

      list<int> sorted_vertex_indices = list_create<int>(5);
      list<int> unsorted_edge_indices = list_create<int>(5);
      list_add(&sorted_vertex_indices, initial_edge.vertex_indices[0]);

      for (int i = 1; i < face->edge_count; i++)
        list_add(&unsorted_edge_indices, face->edge_indices[i]);

      while (unsorted_edge_indices.count > 0) {
        for (int i = 0; i < unsorted_edge_indices.count; i++) {
          int current_edge_index = unsorted_edge_indices.data[i];
          const tb_geometry_edge &current_edge =
              geometry->edges[current_edge_index];

          if (current_edge.vertex_indices[0] ==
              sorted_vertex_indices.data[sorted_vertex_indices.count - 1]) {
            list_add(&sorted_vertex_indices, current_edge.vertex_indices[1]);
            remove_from_list(&unsorted_edge_indices, current_edge_index);
            break;
          } else if (current_edge.vertex_indices[1] ==
                     sorted_vertex_indices
                         .data[sorted_vertex_indices.count - 1]) {
            list_add(&sorted_vertex_indices, current_edge.vertex_indices[0]);
            remove_from_list(&unsorted_edge_indices, current_edge_index);
            break;
          }
        }
      }

      // copy the first element to the end, to close the list
      list_add(&sorted_vertex_indices, sorted_vertex_indices.data[0]);

      // check winding order, reverse if necesarry
      float normal_accumulator[3]{};

      for (int i = 0; i <= sorted_vertex_indices.count - 2; i++) {
        float normal[3]{};

        vec3_cross_product(
            normal,
            geometry->vertices[sorted_vertex_indices.data[i + 0]].position,
            geometry->vertices[sorted_vertex_indices.data[i + 1]].position);

        vec3_add(normal_accumulator, normal_accumulator, normal);
      }

      // normalize the accumulator
      float length_sqr =
          vec3_dot_product(normal_accumulator, normal_accumulator);
      float length = sqrtf(length_sqr);
      normal_accumulator[0] /= length;
      normal_accumulator[1] /= length;
      normal_accumulator[2] /= length;

      bool should_reverse_order =
          vec3_dot_product(face->normal_vector, normal_accumulator) > 0;

      // create vertices
      int *vertex_map = new int[geometry->vertex_count];

      for (int i = 0; i < sorted_vertex_indices.count - 1; i++) {
        vertex_map[sorted_vertex_indices.data[i]] = mesh->vertex_count;
        tb_geometry_vertex geometry_vertex =
            geometry->vertices[sorted_vertex_indices.data[i]];
        float t = (float)i / (float)sorted_vertex_indices.count;

        float uv[2];

        // transform the uvs
        uv[0] = vec3_dot_product(geometry_vertex.position,
                                 face->world_to_uv_matrix[0]);
        uv[1] = vec3_dot_product(geometry_vertex.position,
                                 face->world_to_uv_matrix[1]);
        uv[0] *= face->uv_scale[0];
        uv[1] *= face->uv_scale[1];
        uv[0] += face->uv_offset[0];
        uv[1] += face->uv_offset[1];

        list_add(&mesh->vertex_positions,
                 round_to_int(geometry_vertex.position[0]));
        list_add(&mesh->vertex_positions,
                 round_to_int(geometry_vertex.position[1]));
        list_add(&mesh->vertex_positions,
                 round_to_int(geometry_vertex.position[2]));
        list_add(&mesh->vertex_normals, face->normal_vector[0]);
        list_add(&mesh->vertex_normals, face->normal_vector[1]);
        list_add(&mesh->vertex_normals, face->normal_vector[2]);
        list_add(&mesh->vertex_tangents, face->tangent_vector[0]);
        list_add(&mesh->vertex_tangents, face->tangent_vector[1]);
        list_add(&mesh->vertex_tangents, face->tangent_vector[2]);
        list_add(&mesh->vertex_tangents,
                 0.0f); // todo: make sure this is okay?
        list_add(&mesh->vertex_uvs, uv[0]);
        list_add(&mesh->vertex_uvs, uv[1]);
        mesh->vertex_count++;
      }

      // generate triangle fan from the loop

      for (int i = 1; i < sorted_vertex_indices.count - 2; i++) {
        list_add(&mesh->indices,
                 (unsigned short)vertex_map[sorted_vertex_indices.data[0]]);

        if (should_reverse_order) {
          list_add(
              &mesh->indices,
              (unsigned short)vertex_map[sorted_vertex_indices.data[i + 1]]);
          list_add(
              &mesh->indices,
              (unsigned short)vertex_map[sorted_vertex_indices.data[i + 0]]);
        } else {
          list_add(
              &mesh->indices,
              (unsigned short)vertex_map[sorted_vertex_indices.data[i + 0]]);
          list_add(
              &mesh->indices,
              (unsigned short)vertex_map[sorted_vertex_indices.data[i + 1]]);
        }
      }
    }
  }

  // create final mesh list to return
  list<tb_mesh> tb_meshes = list_create<tb_mesh>(meshes.count);

  for (int mesh_index = 0; mesh_index < meshes.count; mesh_index++) {
    mesh_data *data = &meshes.data[mesh_index];
    tb_mesh tb_mesh;
    tb_mesh.texture_string = data->texture;
    tb_mesh.index_count = data->indices.count;
    tb_mesh.indices = data->indices.data;
    tb_mesh.vertex_count = data->vertex_count;
    tb_mesh.vertex_positions = data->vertex_positions.data;
    tb_mesh.vertex_normals = data->vertex_normals.data;
    tb_mesh.vertex_tangents = data->vertex_tangents.data;
    tb_mesh.vertex_uvs = data->vertex_uvs.data;
    list_add(&tb_meshes, tb_mesh);
  }

  model->meshes = tb_meshes.data;
  model->mesh_count = tb_meshes.count;
}

void tb_model_create(const tb_entity *entity, tb_model *model) {
  void *geometry_buffer = malloc(sizeof(tb_geometry) * entity->brush_count);
  tb_geometry *geometry = (tb_geometry *)geometry_buffer;

  for (int i = 0; i < entity->brush_count; i++)
    tb_geometry_create(&entity->brushes[i], &geometry[i]);

  tb_model_create(geometry, entity->brush_count, model);

  for (int i = 0; i < entity->brush_count; i++)
    tb_geometry_free(&geometry[i]);
}

void tb_model_free(tb_model *model) {
  for (int i = 0; i < model->mesh_count; i++)
    free_mesh(&model->meshes[i]);
}

static void free_mesh(tb_mesh *mesh) {
  // todo: actually free
}

static void cross_product(float *out_result, const float *a, const float *b) {
  out_result[0] = a[1] * b[2] - a[2] * b[1];
  out_result[1] = a[2] * b[0] - a[0] * b[2];
  out_result[2] = a[0] * b[1] - a[1] * b[0];
}

template <typename T> static list<T> list_create(int initial_capacity) {
  list<T> result;
  result.count = 0;
  result.capacity = initial_capacity;
  result.data = (T *)malloc(sizeof(T) * initial_capacity);
  return result;
}

template <typename T> static void list_add(list<T> *list, T value) {
  // check if we are out of space, and resize
  if (list->count >= list->capacity - 1) {
    list->capacity *= 2;
    list->data = (T *)realloc(list->data, list->capacity * sizeof(T));
  }

  list->data[list->count] = value;
  list->count++;
}

template <typename T> static void remove_from_list(list<T> *list, T value) {
  // edge case: we want to remove the last element: just shrink size
  if (list->data[list->count - 1] == value) {
    list->count--;
    return;
  }

  // common case: search the list for value, shift everything into its spot if
  // found
  for (int i = 0; i < list->count - 1; i++) {
    if (list->data[i] == value) {
      memmove(&list->data[i], &list->data[i + 1],
              (list->count - 1 - i) * sizeof(T));
      list->count--;
      return;
    }
  }
}

static float lerp(float a, float b, float t) { return (1 - t) * a + t * b; }

static void vec3_add(float *out_result, const float *a, const float *b) {
  out_result[0] = a[0] + b[0];
  out_result[1] = a[1] + b[1];
  out_result[2] = a[2] + b[2];
}

static float vec3_dot_product(const float *a, const float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vec3_cross_product(float *out_result, const float *a,
                               const float *b) {
  out_result[0] = a[1] * b[2] - a[2] * b[1];
  out_result[1] = a[2] * b[0] - a[0] * b[2];
  out_result[2] = a[0] * b[1] - a[1] * b[0];
}

static void vec3_subtract(float *out_result, const float *a, const float *b) {
  out_result[0] = a[0] - b[0];
  out_result[1] = a[1] - b[1];
  out_result[2] = a[2] - b[2];
}

// todo: I still see edges in the render: investigate this?
static float round_to_int(float value) { return (float)((int)roundf(value)); }

static void create_mesh_data(mesh_data *data, string_handle texture) {
  data->vertex_count = 0;
  data->texture = texture;
  data->vertex_positions = list_create<float>(10);
  data->vertex_tangents = list_create<float>(10);
  data->vertex_normals = list_create<float>(10);
  data->vertex_uvs = list_create<float>(10);
  data->indices = list_create<unsigned short>(10);
}