#include "tb_geometry.hpp"
#include <stdlib.h> // for malloc/free
#include <string.h> // for memcpy

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

static const int WORLD_SIZE = 100000;

template <typename T> struct list {
  T *data;
  int count;
  int capacity;
};

struct face_info {
  string_handle texture;
  float world_to_uv_matrix[4][4];
  float uv_scale[2];
  float uv_offset[2];
  float normal[3];
  float tangent[3];
  float point[3];
};

struct clipping_vertex {
  float position[3];
  float distance{0};
  int occurs{0};
  bool is_visible{true};
};

struct clipping_edge {
  int vertex_indices[2]{-1, -1};
  int face_indices[2]{-1, -1};
  bool is_visible{true};
};

struct clipping_face {
  list<int> edge_indices{};
  bool is_visible{true};
  face_info info{};
};

struct clipping_geometry {
  list<clipping_face> faces{};
  list<clipping_vertex> vertices{};
  list<clipping_edge> edges{};
};

enum clip_result {
  RESULT_NO_CLIPPING,
  RESULT_TOTAL_CLIPPING,
  RESULT_PARTIAL_CLIPPING,
};

static float lerp(float a, float b, float t);
static void vec3_add(float *out_result, const float *a, const float *b);
static void vec3_subtract(float *out_result, const float *a, const float *b);
static clipping_face create_face(int edge_1, int edge_2, int edge_3,
                                 int edge_4);
template <typename T> static void list_add(list<T> *list, T value);
template <typename T> static list<T> list_create(int initial_capacity);
template <typename T> static void remove_from_list(list<T> *list, T value);
static clip_result clip_geometry(clipping_geometry *geometry, face_info info);
static void get_geometry_from_bounds(clipping_geometry *out_geometry,
                                     const float *center,
                                     const float *half_extents);
static float vec3_dot_product(const float *a, const float *b);

void tb_geometry_create(const tb_brush *brush, tb_geometry *out_geometry) {
  // generate the edges, faces, ect for a cube the size of our entire world
  float world_center[]{0, 0, 0};
  float world_extents[]{WORLD_SIZE, WORLD_SIZE, WORLD_SIZE};

  clipping_geometry geometry;
  get_geometry_from_bounds(&geometry, world_center, world_extents);

  // clip away the geometry with each plane in the brush
  for (int i = 0; i < brush->face_count; i++) {
    tb_brush_face *face = &brush->faces[i];

    face_info info;
    memcpy(info.point, face->plane[0], sizeof(float) * 3);
    tb_get_face_normal(info.normal, face);
    tb_get_face_tangent(info.tangent, face);
    info.texture = face->texture;

    // hand-compted uv matrix: change of basis to the UV normals, translate, and
    // scale
    // float u_scale = 1 / (face->u_scale * 32);
    // float v_scale = 1 / (face->v_scale * 32);
    // float u_scale = face->u_scale / 2;
    // float v_scale = face->v_scale / 2;

    info.world_to_uv_matrix[0][0] = face->u_normal[0];
    info.world_to_uv_matrix[0][1] = face->u_normal[1];
    info.world_to_uv_matrix[0][2] = face->u_normal[2];
    // info.world_to_uv_matrix[0][3] = face->u_offset;
    info.world_to_uv_matrix[1][0] = face->v_normal[0];
    info.world_to_uv_matrix[1][1] = face->v_normal[1];
    info.world_to_uv_matrix[1][2] = face->v_normal[2];
    // info.world_to_uv_matrix[1][3] = face->v_offset;
    memset(&info.world_to_uv_matrix[2][0], 0, sizeof(float) * 7);
    info.world_to_uv_matrix[3][3] = 1;
    info.uv_offset[0] = face->u_offset;
    info.uv_offset[1] = face->v_offset;
    info.uv_scale[0] = 1 / face->u_scale;
    info.uv_scale[1] = 1 / face->v_scale;

    clip_geometry(&geometry, info);
  }

  // convert clipping data into geometry
  list<tb_geometry_face> faces = list_create<tb_geometry_face>(6);
  list<tb_geometry_edge> edges = list_create<tb_geometry_edge>(12);
  list<tb_geometry_vertex> vertices = list_create<tb_geometry_vertex>(8);

  int *edge_map = (int *)malloc(sizeof(int) * geometry.edges.count);
  int *vertex_map = (int *)malloc(sizeof(int) * geometry.vertices.count);
  int *face_map = (int *)malloc(sizeof(int) * geometry.faces.count);

  for (int vertex_index = 0; vertex_index < geometry.vertices.count;
       vertex_index++) {
    const clipping_vertex &c_vertex = geometry.vertices.data[vertex_index];

    if (!c_vertex.is_visible)
      continue;

    vertex_map[vertex_index] = vertices.count;
    tb_geometry_vertex vertex{};
    memcpy(vertex.position, &c_vertex.position, sizeof(float) * 3);
    list_add(&vertices, vertex);
  }

  for (int edge_index = 0; edge_index < geometry.edges.count; edge_index++) {
    const clipping_edge &c_edge = geometry.edges.data[edge_index];

    if (!c_edge.is_visible)
      continue;

    edge_map[edge_index] = edges.count;
    tb_geometry_edge edge{};
    edge.vertex_indices[0] = vertex_map[c_edge.vertex_indices[0]];
    edge.vertex_indices[1] = vertex_map[c_edge.vertex_indices[1]];
    list_add(&edges, edge);
  }

  for (int face_index = 0; face_index < geometry.faces.count; face_index++) {
    const clipping_face &c_face = geometry.faces.data[face_index];

    if (!c_face.is_visible)
      continue;

    face_map[face_index] = faces.count;
    tb_geometry_face face{};
    face.texture = c_face.info.texture;
    face.edge_count = c_face.edge_indices.count;
    face.edge_indices = (int *)malloc(sizeof(int) * face.edge_count);

    for (int i = 0; i < c_face.edge_indices.count; i++)
      face.edge_indices[i] = edge_map[c_face.edge_indices.data[i]];

    memcpy(face.normal_vector, c_face.info.normal, sizeof(float) * 3);
    memcpy(face.tangent_vector, c_face.info.tangent, sizeof(float) * 3);
    memcpy(face.world_to_uv_matrix, c_face.info.world_to_uv_matrix,
           sizeof(float) * 4 * 4);
    memcpy(face.uv_offset, c_face.info.uv_offset, sizeof(float) * 2);
    memcpy(face.uv_scale, c_face.info.uv_scale, sizeof(float) * 2);
    list_add(&faces, face);
  }

  // remap the edges with our newly created faces
  for (int i = 0; i < edges.count; i++) {
    edges.data[i].face_indices[0] = face_map[edges.data[i].face_indices[0]];
    edges.data[i].face_indices[1] = face_map[edges.data[i].face_indices[1]];
  }

  out_geometry->edge_count = edges.count;
  out_geometry->edges = edges.data;
  out_geometry->face_count = faces.count;
  out_geometry->faces = faces.data;
  out_geometry->vertex_count = vertices.count;
  out_geometry->vertices = vertices.data;
}

void tb_geometry_free(tb_geometry *geometry) {
  for (int i = 0; i < geometry->face_count; i++)
    free(geometry->faces[i].edge_indices);

  free(geometry->edges);
  free(geometry->faces);
  free(geometry->vertices);
}

static void get_geometry_from_bounds(clipping_geometry *out_geometry,
                                     const float *center,
                                     const float *half_extents) {
  float min[3];
  vec3_subtract(min, center, half_extents);

  float max[3];
  vec3_add(max, center, half_extents);

  out_geometry->vertices = list_create<clipping_vertex>(8);
  out_geometry->edges = list_create<clipping_edge>(12);
  out_geometry->faces = list_create<clipping_face>(6);

  list_add(&out_geometry->vertices,
           {min[0], min[1], min[2]}); /* [0] front bottom left  */
  list_add(&out_geometry->vertices,
           {min[0], max[1], min[2]}); /* [1] front top left     */
  list_add(&out_geometry->vertices,
           {max[0], max[1], min[2]}); /* [2] front top right    */
  list_add(&out_geometry->vertices,
           {max[0], min[1], min[2]}); /* [3] front bottom right */
  list_add(&out_geometry->vertices,
           {min[0], min[1], max[2]}); /* [4] back bottom left   */
  list_add(&out_geometry->vertices,
           {min[0], max[1], max[2]}); /* [5] back top left      */
  list_add(&out_geometry->vertices,
           {max[0], max[1], max[2]}); /* [6] back top right     */
  list_add(&out_geometry->vertices,
           {max[0], min[1], max[2]}); /* [7] back bottom right  */

  list_add(&out_geometry->edges, {{0, 3}, {0, 5}}); /*  [0] front-bottom */
  list_add(&out_geometry->edges, {{1, 2}, {0, 4}}); /*  [1] front-top */
  list_add(&out_geometry->edges, {{0, 1}, {0, 2}}); /*  [2] front-left */
  list_add(&out_geometry->edges, {{2, 3}, {0, 3}}); /*  [3] front-right */
  list_add(&out_geometry->edges, {{4, 7}, {1, 5}}); /*  [4] back-bottom */
  list_add(&out_geometry->edges, {{5, 6}, {1, 4}}); /*  [5] back-top */
  list_add(&out_geometry->edges, {{4, 5}, {1, 2}}); /*  [6] back-left */
  list_add(&out_geometry->edges, {{6, 7}, {1, 3}}); /*  [7] back-right */
  list_add(&out_geometry->edges, {{0, 4}, {5, 2}}); /*  [8] side-bottom-left  */
  list_add(&out_geometry->edges, {{1, 5}, {4, 2}}); /*  [9] side-top-left */
  list_add(&out_geometry->edges, {{3, 7}, {5, 3}}); /* [10] side-bottom-right */
  list_add(&out_geometry->edges, {{2, 6}, {4, 3}}); /* [11] side-top-right */

  list_add(&out_geometry->faces, create_face(0, 1, 2, 3));   /* [0] front  */
  list_add(&out_geometry->faces, create_face(4, 5, 6, 7));   /* [1] back   */
  list_add(&out_geometry->faces, create_face(2, 6, 8, 9));   /* [2] left   */
  list_add(&out_geometry->faces, create_face(3, 7, 10, 11)); /* [3] right  */
  list_add(&out_geometry->faces, create_face(1, 5, 9, 11));  /* [4] top    */
  list_add(&out_geometry->faces, create_face(0, 4, 8, 10));  /* [5] bottom */
}

static clip_result clip_geometry(clipping_geometry *geometry, face_info info) {
  // used for floating point comparisons to avoid error with small values
  const float k_epsilon = 0.01f;

  // step one: process vertices (check distance from plane)
  int count_clipped = 0;
  int count_total = 0;

  for (int i = 0; i < geometry->vertices.count; i++) {
    clipping_vertex &vertex = geometry->vertices.data[i];

    if (!vertex.is_visible)
      continue;

    count_total++;
    float c = vec3_dot_product(info.normal, info.point);
    vertex.distance = vec3_dot_product(info.normal, vertex.position) - c;

    if (vertex.distance >= k_epsilon) {
      count_clipped++;
      vertex.is_visible = false;
    } else if (vertex.distance >= -k_epsilon)
      vertex.distance = 0;
  }

  // we can early-exit in a couple of easy edge cases
  if (count_clipped == 0)
    return RESULT_NO_CLIPPING;

  else if (count_clipped == count_total)
    return RESULT_TOTAL_CLIPPING;

  // step two: process edges (split if intersected)
  for (int edge_index = 0; edge_index < geometry->edges.count; edge_index++) {
    clipping_edge &edge = geometry->edges.data[edge_index];

    if (!edge.is_visible)
      continue;

    const clipping_vertex &vertex_1 =
        geometry->vertices.data[edge.vertex_indices[0]];
    const clipping_vertex &vertex_2 =
        geometry->vertices.data[edge.vertex_indices[1]];

    if (!vertex_1.is_visible && !vertex_2.is_visible) {
      // the edge is fully culled
      edge.is_visible = false;

      // remove it from adjacent faces
      for (int i = 0; i < ARR_LEN(edge.face_indices); i++) {
        clipping_face &face = geometry->faces.data[edge.face_indices[i]];
        list<int> &edges = face.edge_indices;
        remove_from_list(&edges, edge_index);

        if (edges.count == 0)
          face.is_visible = false;
      }
    } else if (vertex_1.is_visible && vertex_2.is_visible) {
      // edge is fully visible, nothing to do
      continue;
    } else // edge is half split
    {
      // todo: understand this block of code better
      float t = vertex_1.distance / (vertex_1.distance - vertex_2.distance);
      int index = geometry->vertices.count;
      clipping_vertex new_vertex{};
      new_vertex.position[0] =
          lerp(vertex_1.position[0], vertex_2.position[0], t);
      new_vertex.position[1] =
          lerp(vertex_1.position[1], vertex_2.position[1], t);
      new_vertex.position[2] =
          lerp(vertex_1.position[2], vertex_2.position[2], t);
      new_vertex.distance = 0;
      new_vertex.occurs = 0;
      new_vertex.is_visible = true;

      if (vertex_1.is_visible)
        edge.vertex_indices[1] = index;

      else
        edge.vertex_indices[0] = index;

      list_add(&geometry->vertices, new_vertex);
    }
  }

  // step three: process faces (connect if missing an edge, close hole with new
  // face)
  clipping_face new_face;
  new_face.edge_indices = list_create<int>(3);
  new_face.info = info;
  new_face.is_visible = true;
  new_face.info.texture = info.texture;
  int new_face_index = geometry->faces.count;

  for (int face_index = 0; face_index < geometry->faces.count; face_index++) {
    clipping_face &face = geometry->faces.data[face_index];

    if (!face.is_visible)
      continue;

    int start = -1;
    int final = -1;

    for (int i = 0; i < face.edge_indices.count; i++) {
      clipping_edge &edge = geometry->edges.data[face.edge_indices.data[i]];
      geometry->vertices.data[edge.vertex_indices[0]].occurs = 0;
      geometry->vertices.data[edge.vertex_indices[1]].occurs = 0;
    }

    for (int i = 0; i < face.edge_indices.count; i++) {
      clipping_edge &edge = geometry->edges.data[face.edge_indices.data[i]];
      geometry->vertices.data[edge.vertex_indices[0]].occurs++;
      geometry->vertices.data[edge.vertex_indices[1]].occurs++;
    }

    int corner_count = 0;

    for (int i = 0; i < face.edge_indices.count; i++) {
      clipping_edge &edge = geometry->edges.data[face.edge_indices.data[i]];
      if (geometry->vertices.data[edge.vertex_indices[0]].occurs == 1)
        corner_count++;
      if (geometry->vertices.data[edge.vertex_indices[1]].occurs == 1)
        corner_count++;
    }

    for (int i = 0; i < face.edge_indices.count; i++) {
      clipping_edge &edge = geometry->edges.data[face.edge_indices.data[i]];
      int vertex_index_1 = edge.vertex_indices[0];
      int vertex_index_2 = edge.vertex_indices[1];
      clipping_vertex &vertex_1 = geometry->vertices.data[vertex_index_1];
      clipping_vertex &vertex_2 = geometry->vertices.data[vertex_index_2];

      if (vertex_1.occurs == 1) {
        if (start == -1) {
          start = vertex_index_1;
          vertex_1.occurs++;
        } else if (final == -1) {
          final = vertex_index_1;
          vertex_1.occurs++;
        }
      }

      if (vertex_2.occurs == 1) {
        if (start == -1) {
          start = vertex_index_2;
          vertex_2.occurs++;
        } else if (final == -1) {
          final = vertex_index_2;
          vertex_2.occurs++;
        }
      }
    }

    if (start != -1) {
      clipping_edge new_edge;
      new_edge.is_visible = true;
      new_edge.vertex_indices[0] = start;
      new_edge.vertex_indices[1] = final;
      new_edge.face_indices[0] = face_index;
      new_edge.face_indices[1] = new_face_index;

      int new_edge_index = geometry->edges.count;
      list_add(&face.edge_indices, new_edge_index);
      list_add(&new_face.edge_indices, new_edge_index);
      list_add(&geometry->edges, new_edge);
    }
  }

  list_add(&geometry->faces, new_face);
  return RESULT_PARTIAL_CLIPPING;
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

static clipping_face create_face(int edge_1, int edge_2, int edge_3,
                                 int edge_4) {
  clipping_face face{};
  face.edge_indices = list_create<int>(4);
  list_add(&face.edge_indices, edge_1);
  list_add(&face.edge_indices, edge_2);
  list_add(&face.edge_indices, edge_3);
  list_add(&face.edge_indices, edge_4);
  face.is_visible = true;
  return face;
}

static void vec3_add(float *out_result, const float *a, const float *b) {
  out_result[0] = a[0] + b[0];
  out_result[1] = a[1] + b[1];
  out_result[2] = a[2] + b[2];
}

static void vec3_subtract(float *out_result, const float *a, const float *b) {
  out_result[0] = a[0] - b[0];
  out_result[1] = a[1] - b[1];
  out_result[2] = a[2] - b[2];
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

static float vec3_dot_product(const float *a, const float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float lerp(float a, float b, float t) { return (1 - t) * a + t * b; }