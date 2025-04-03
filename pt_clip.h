#ifndef PT_CLIP_H
#define PT_CLIP_H

#define PTC_REAL float

typedef enum ptc_winding {
  PTC_WINDING_ANY,
  PTC_WINDING_CW,
  PTC_WINDING_CCW,
} ptc_winding;

typedef struct ptc_plane {
  PTC_REAL normal[3];
  PTC_REAL c;
} ptc_plane;

typedef struct ptc_vertex {
  PTC_REAL position[3];
  float distance;
  int is_clipped;
  int occurs;
} ptc_vertex;

typedef struct ptc_edge {
  int vertices[2];
  int faces[2];
  int is_clipped;
} ptc_edge;

typedef struct ptc_face {
  int* edges;
  int edge_count;
  PTC_REAL normal[3];
  void* userdata;
  int is_clipped;
} ptc_face;

typedef struct ptc_mesh {
  ptc_vertex* vertices;
  int vertex_count;
  int vertex_capacity;

  ptc_edge* edges;
  int edge_count;
  int edge_capacity;

  ptc_face* faces;
  int face_count;
  int face_capacity;
} ptc_mesh;

void ptc_init_bounds(ptc_mesh* mesh, PTC_REAL min[3], PTC_REAL max[3]);
void ptc_free(ptc_mesh* mesh);
void ptc_clip(ptc_mesh* mesh, ptc_plane* plane, void* userdata);
int ptc_get_vertices(ptc_mesh* mesh, int face, int* vertices, ptc_winding winding);

#endif // PT_CLIP_H

#define PT_CLIP_IMPLEMENTATION
#ifdef PT_CLIP_IMPLEMENTATION

#ifndef PTC_ASSERT
  #include <assert.h>
  #define PTC_ASSERT(expr) assert(expr)
#endif

#ifndef PTC_MALLOC
  #include <stdlib.h>
  #define PTC_MALLOC(bytes) malloc(bytes)
  #define PTC_REALLOC(block, bytes) realloc(block, bytes)
  #define PTC_FREE(block) free(block)
#endif

#ifndef PTC_SQRTF
  #include <math.h>
  #if PTC_REAL == double
    #define PTC_SQRTR(d) sqrt(d)
  #elif PTC_REAL == float
    #define PTC_SQRTR(f) sqrtf(f)
  #endif
#endif

static void ptc__zero_memory(void* dest, int bytes) {
  for (int i = 0; i < bytes; i++) {
    ((char*)dest)[i] = 0;
  }
}

static void ptc__copy_memory(void* dest, void* src, int bytes) {
  for (int i = 0; i < bytes; i++) {
    ((char*)dest)[i] = ((char*)src)[i];
  }
}

static PTC_REAL ptc__dot_product(PTC_REAL* a, PTC_REAL* b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void ptc__cross_product(PTC_REAL* result, PTC_REAL* a, PTC_REAL* b) {
}

static void ptc__lerp(PTC_REAL* result, PTC_REAL* from, PTC_REAL* to, float t) {
  float invt = (1 - t);
  result[0] = invt * from[0] + t * to[0];
  result[1] = invt * from[1] + t * to[1];
  result[2] = invt * from[2] + t * to[2];
}

static PTC_REAL ptc__plane_distance(ptc_plane* plane, PTC_REAL* position) {
  return ptc__dot_product(plane->normal, position) - plane->c;
}

static void ptc__add_face_edge(ptc_mesh* mesh, int face, int edge) {
  if (face == -1) return;

  // Add the edge to the face's internal list
  ptc_face* f = &mesh->faces[face];
  f->edge_count++;
  f->edges = (int*)PTC_REALLOC(f->edges, sizeof(ptc_edge) * f->edge_count);
  f->edges[f->edge_count - 1] = edge;

  // Add the face to the edge's internal list
  ptc_edge* e = &mesh->edges[edge];
  if (e->faces[0] == -1) e->faces[0] = face;
  else if (e->faces[1] == -1) e->faces[1] = face;
  else PTC_ASSERT(0);
}

static void ptc__remove_face_edge(ptc_mesh* mesh, int face, int edge) {
  if (face == -1) return;
  ptc_face* f = &mesh->faces[face];

  // If we want to remove the final edge, we can simply
  // decrease our count. Otherwise, we swap the final
  // edge into the removed edge's place.
  // (note: this makes the edge list unstable, which happens to be okay)
  int last = f->edge_count - 1;

  if (f->edges[last] != edge) {
    for (int i = 0; i < f->edge_count; i++) {
      if (f->edges[i] == edge) {
        f->edges[i] = f->edges[last];
        break;
      }
    }
  }

  f->edge_count--;

  // Remove the face if all it's edges have been removed
  if (f->edge_count <= 0) {
    f->is_clipped = 1;
  }
  else {
    f->edges = (int*)PTC_REALLOC(f->edges, sizeof(ptc_edge) * f->edge_count);
  }
}

static int ptc__add_vertex(ptc_mesh* mesh, PTC_REAL* position) {
  mesh->vertex_count++;
  int capacity = mesh->vertex_capacity;
  int count = mesh->vertex_count;

  if (capacity < count + 1) {
    mesh->vertex_capacity = (capacity == 0) ? 10 : capacity * 2;
    mesh->vertices = (ptc_vertex*)PTC_REALLOC(mesh->vertices, sizeof(ptc_vertex)*capacity);
  }

  ptc_vertex* vertex = &mesh->vertices[count - 1];
  ptc__zero_memory(vertex, sizeof vertex);
  ptc__copy_memory(vertex->position, position, sizeof vertex->position);

  return count - 1;
}

static int ptc__add_edge(ptc_mesh* mesh, int v0, int v1) {
  mesh->edge_count++;
  int capacity = mesh->edge_capacity;
  int count = mesh->edge_count;

  if (capacity < count + 1) {
    mesh->edge_capacity = (capacity == 0) ? 10 : capacity * 2;
    mesh->edges = (ptc_edge*)PTC_REALLOC(mesh->edges, sizeof(ptc_edge)*capacity);
  }

  ptc_edge* edge = &mesh->edges[count - 1];
  ptc__zero_memory(edge, sizeof edge);
  edge->faces[0] = -1;
  edge->faces[1] = -1;
  edge->vertices[0] = v0;
  edge->vertices[1] = v1;

  return count - 1;
}

static int ptc__add_face(ptc_mesh* mesh, PTC_REAL normal[3], void* userdata) {
  mesh->face_count++;
  int capacity = mesh->face_capacity;
  int count = mesh->face_count;

  if (capacity < count + 1) {
    mesh->face_capacity = (capacity == 0) ? 10 : capacity * 2;
    mesh->faces = (ptc_face*)PTC_REALLOC(mesh->faces, sizeof(ptc_face)*capacity);
  }

  ptc_face* face = &mesh->faces[count - 1];
  ptc__zero_memory(face, sizeof *face);
  ptc__copy_memory(face->normal, normal, sizeof(PTC_REAL) * 3);
  face->userdata = userdata;

  return count - 1;
}

static void ptc__init_face(ptc_face* face, int e0, int e1, int e2, int e3, PTC_REAL n0, PTC_REAL n1, PTC_REAL n2) {
  ptc__zero_memory(face, sizeof *face);
  face->edge_count = 4;
  face->edges = (int*)PTC_MALLOC(4 * sizeof(int));
  face->edges[0] = e0;
  face->edges[1] = e1;
  face->edges[2] = e2;
  face->edges[3] = e3;
  face->normal[0] = n0;
  face->normal[1] = n1;
  face->normal[2] = n2;
}

static void ptc__init_vertex(ptc_vertex* vertex, PTC_REAL x, PTC_REAL y, PTC_REAL z) {
  ptc__zero_memory(vertex, sizeof *vertex);
  vertex->position[0] = x;
  vertex->position[1] = y;
  vertex->position[2] = z;
}

static void ptc__init_edge(ptc_edge* edge, int v0, int v1, int f0, int f1) {
  ptc__zero_memory(edge, sizeof *edge);
  edge->vertices[0] = v0;
  edge->vertices[1] = v1;
  edge->faces[0] = f0;
  edge->faces[1] = f1;
}

void ptc_init_bounds(ptc_mesh* mesh, PTC_REAL min[3], PTC_REAL max[3]) {
  mesh->vertex_count = 8;
  ptc_vertex* v = mesh->vertices;
  ptc__init_vertex(&v[0], min[0], min[1], min[2]); // front bottom left
  ptc__init_vertex(&v[1], min[0], max[1], min[2]); // front top left
  ptc__init_vertex(&v[2], max[0], max[1], min[2]); // front top right
  ptc__init_vertex(&v[3], max[0], min[1], min[2]); // front bottom 
  ptc__init_vertex(&v[4], min[0], min[1], max[2]); // back bottom left
  ptc__init_vertex(&v[5], min[0], max[1], max[2]); // back top left
  ptc__init_vertex(&v[6], max[0], max[1], max[2]); // back top right
  ptc__init_vertex(&v[7], max[0], min[1], max[2]); // back bottom right

  mesh->edge_count = 12;
  ptc_edge* e = mesh->edges;
  ptc__init_edge(&e[0], 0, 3, 0, 5); // front-bottom 
  ptc__init_edge(&e[1], 1, 2, 0, 4); // front-top 
  ptc__init_edge(&e[2], 0, 1, 0, 2); // front-left 
  ptc__init_edge(&e[3], 2, 3, 0, 3); // front-right 
  ptc__init_edge(&e[4], 4, 7, 1, 5); // back-bottom 
  ptc__init_edge(&e[5], 5, 6, 1, 4); // back-top 
  ptc__init_edge(&e[6], 4, 5, 1, 2); // back-left 
  ptc__init_edge(&e[7], 6, 7, 1, 3); // back-right 
  ptc__init_edge(&e[8], 0, 4, 5, 2); // side-bottom-left 
  ptc__init_edge(&e[9], 1, 5, 4, 2); // side-top-left 
  ptc__init_edge(&e[10],3, 7, 5, 3); // side-bottom-right
  ptc__init_edge(&e[11],2, 6, 4, 3); // side-top-right 

  mesh->face_count = 6;
  ptc_face* f = mesh->faces;
  ptc__init_face(&f[0], 0, 1, 2, 3, 0, 0,-1);   // front
  ptc__init_face(&f[1], 4, 5, 6, 7, 0, 0, 1);   // back
  ptc__init_face(&f[2], 2, 6, 8, 9, -1, 0, 0);   // left
  ptc__init_face(&f[3], 3, 7, 10, 11, 1, 0, 0); // right
  ptc__init_face(&f[4], 1, 5, 9, 11, 0, 1, 0);  // top
  ptc__init_face(&f[5], 0, 4, 8, 10, 0,-1, 0);  // bottom
}

void ptc_free(ptc_mesh* mesh) {
  for (int i = 0; i < mesh->face_count; i++) {
    PTC_FREE(mesh->faces[i].edges);
  }
  PTC_FREE(mesh->faces);
  PTC_FREE(mesh->edges);
  PTC_FREE(mesh->vertices);
}

void ptc_clip(ptc_mesh* mesh, ptc_plane* plane, void* userdata) {
  const PTC_REAL EPSILON = 0.01f;

  int count_clipped = 0;
  int count_total = 0;

  // Step one: Calculate the distance of each vertex from the clipping plane.
  // If the vertex falls on the positive side of the clipping plane, we "clip" it
  // by making it invisible.
  for (int i = 0; i < mesh->vertex_count; i++) {
    ptc_vertex* vertex = &mesh->vertices[i];

    if (vertex->is_clipped) { 
      continue; 
    }

    count_total++;
    vertex->distance = ptc__plane_distance(plane, vertex->position);

    if (vertex->distance >= EPSILON) {
      count_clipped++;
      vertex->is_clipped = 1;
    }
    else if (vertex->distance >= -EPSILON) {
      // Snap the distance to 0 if it's really small.
      vertex->distance = 0;
    }
  }

  int is_nothing_clipped = count_clipped == 0;
  int is_everything_clipped = count_clipped == count_total;

  // A couple easy edge cases that can save us some work
  if (is_nothing_clipped || is_everything_clipped) {
    return;
  }

  // Step two: Determine the visibility of each edge.
  for (int edge_idx = 0; edge_idx < mesh->edge_count; edge_idx++) {
    ptc_edge* edge = &mesh->edges[edge_idx];

    if (edge->is_clipped) {
      continue;
    }

    ptc_vertex* v0 = &mesh->vertices[edge->vertices[0]];
    ptc_vertex* v1 = &mesh->vertices[edge->vertices[1]];

    if (v0->is_clipped && v1->is_clipped) {
      // The edge lost both of it's vertices: it is completely clipped.
      edge->is_clipped = 1;
      ptc__remove_face_edge(mesh, edge->faces[0], edge_idx);
      ptc__remove_face_edge(mesh, edge->faces[1], edge_idx);
    }
    else if (!v0->is_clipped && !v1->is_clipped) {
      // The edge is fully visible: no need to do anything.
      continue;
    }
    else {
      // The edge lost one of its two vertices: it is half-split.
      // Calculate the midpoint at which the edge is split.
      // The calculation of "t" can be visualized like this:
      //
      //        v0 = = = = | = = = = v1
      //        |----[d0]--|
      //                   |--[d1]----|
      //        |-------[d0-d1]-------|
      // 
      float d0 = v0->distance;
      float d1 = v1->distance;
      float t =  d0 / (d0 - d1);
      PTC_REAL midpoint[3];
      ptc__lerp(midpoint, v0->position, v1->position, t);

      // Create a new visible vertex at the midpoint.
      // New edges to connect the new vertices are created later,
      // during face processing.
      int new_vertex = ptc__add_vertex(mesh, midpoint);

      // Replace whichever vertex was clipped in this edge
      // with the new one.
      edge->vertices[v0->is_clipped ? 0 : 1] = new_vertex;
    }
  }

  // Step three: Create new edges to connect the newly 
  // created vertices into the existing faces. 
  // We also create a new face to close the created hole.
  int new_face_idx = ptc__add_face(mesh, plane->normal, userdata);
  ptc_face* new_face = &mesh->faces[new_face_idx];

  for (int face_idx = 0; face_idx < mesh->face_count; face_idx++) {
    ptc_face* face = &mesh->faces[face_idx];

    if (face->is_clipped) {
      continue;
    }

    // Determine if the face is missing an edge.
    // This problem is solved by counting how many times each vertex occurs in an edge.
    // In a closed loop, each vertex occurs exactly twice (once in each edge it connects to).
    // In an open loop, there will be some vertices that only occur once.
    for (int i = 0; i < face->edge_count; i++) {
      ptc_edge* edge = &mesh->edges[face->edges[i]];
      mesh->vertices[edge->vertices[0]].occurs = 0;
      mesh->vertices[edge->vertices[1]].occurs = 0;
    }

    for (int i = 0; i < face->edge_count; i++) {
      ptc_edge* edge = &mesh->edges[face->edges[i]];
      mesh->vertices[edge->vertices[0]].occurs++;
      mesh->vertices[edge->vertices[1]].occurs++;
    }

    // A vertex that only occurs once is an "endpoint".
    // If there are only two endpoints, and we connect them with a new edge, we have closed the loop.
    int endpoints[2] = {-1, -1};

    for (int i = 0; i < face->edge_count; i++) {
      ptc_edge* edge = &mesh->edges[face->edges[i]];

      int endpoint = -1;
      if (mesh->vertices[edge->vertices[0]].occurs == 1) endpoint = edge->vertices[0];
      if (mesh->vertices[edge->vertices[1]].occurs == 1) endpoint = edge->vertices[1];

      // We can ignore this edge if it does not contain an endpoint
      if (endpoint == -1) {
        continue;
      }

      // Scan for an available endpoint (one that is still defaulted to -1).
      int available_endpoint = -1;

      for (int i = 0; i < 2; i++) {
        if (endpoints[i] == -1) {
          available_endpoint = i;
          break;
        }
      }

      // In this case, there must be more than one
      // missing edges, which should be impossible
      // when clipping by a single plane.
      PTC_ASSERT(available_endpoint != -1);

      endpoints[available_endpoint] = endpoint;
    }

    // Create the new edge to complete the face, if needed
    if (endpoints[0] != -1 && endpoints[1] != -1) {
      int edge = ptc__add_edge(mesh, endpoints[0], endpoints[1]);
      ptc__add_face_edge(mesh, face_idx, edge);
      ptc__add_face_edge(mesh, new_face_idx, edge);
    }
  }
}

int ptc_get_vertices(ptc_mesh* mesh, int face, int* vertices, ptc_winding target_winding) {
  ptc_face* f = &mesh->faces[face];

  if (vertices != NULL) {
    vertices[0] = mesh->edges[f->edges[0]].vertices[0];
    vertices[1] = mesh->edges[f->edges[0]].vertices[1];

    // Populate the remaining vertices such that they form a loop
    for (int i = 1; i < f->edge_count; i++) {
      for (int j = 0; j < f->edge_count; j++) {
        ptc_edge* e = &mesh->edges[f->edges[j]];
        if (e->vertices[0] == vertices[i] && e->vertices[1] != vertices[i - 1]) {
          vertices[i + 1] = e->vertices[1];
          break;
        }
        else if (e->vertices[1] == vertices[i] && e->vertices[0] != vertices[i - 1]) {
          vertices[i + 1] = e->vertices[0];
          break;
        }
      }
    }

    if (target_winding != PTC_WINDING_ANY) {
      // Calculate current winding order
      PTC_REAL normal_accumulator[3] = {0};

      for (int i = 0; i <= f->edge_count - 1; i++) {
        PTC_REAL normal[3] = {0};
        PTC_REAL* p0 = mesh->vertices[vertices[i + 0]].position;
        PTC_REAL* p1 = mesh->vertices[vertices[i + 1]].position;
        ptc__cross_product(normal, p0, p1);

        normal_accumulator[0] += normal[0];
        normal_accumulator[1] += normal[1];
        normal_accumulator[2] += normal[2];
      }

      // normalize the accumulator
      PTC_REAL length_sqr = ptc__dot_product(normal_accumulator, normal_accumulator);
      PTC_REAL length = PTC_SQRTR(length_sqr);
      normal_accumulator[0] /= length;
      normal_accumulator[1] /= length;
      normal_accumulator[2] /= length;

      PTC_REAL dot = ptc__dot_product(f->normal, normal_accumulator);
      // todo: verify or swap this is correct
      ptc_winding current_winding = dot > 0 ? PTC_WINDING_CCW : PTC_WINDING_CW;

      if (target_winding != current_winding) {
        // reverse vertices
        for (int i = 0; i < (f->edge_count + 1) / 2; i++) {
          int temp = vertices[i];
          vertices[i] = vertices[f->edge_count - i];
          vertices[f->edge_count - i] = temp;
        }
      }
    }
  }

  return f->edge_count + 1;
}
  
#endif // PT_CLIP_IMPLEMENTATION
