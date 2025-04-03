#include "poelib.hpp"
#include <assert.h>

struct ClippingVertex : GeometryVertex {
  bool isVisible;
};

struct ClippingEdge : GeometryEdge {
  bool isVisible;
};

// Cannot extend GeometryFace because it uses a fixed-length
// array to store edges, while the clipper needs to add new
// edges dynamically.
struct ClippingFace {
  DynamicList<int> edgeIndices;
  Vector3 normal;
  bool isVisible;
};

struct ClippingGeometry {
  DynamicList<ClippingVertex> vertices;
  DynamicList<ClippingEdge> edges;
  DynamicList<ClippingFace> faces;
  int visibleVertexCount;
  int visibleEdgeCount;
  int visibleFaceCount;
};

constexpr int WORLD_SIZE = 10000;

static Array<int> CreateArray(int a, int b, int c, int d, Arena* storage);
static ClippingGeometry CreateClippingGeometry(Geometry* geometry);
static void FreeClippingGeometry(ClippingGeometry* geometry);
static Geometry* CreateGeometry(ClippingGeometry* geometry, Arena* storage);
static void Clip(ClippingGeometry* geometry, Plane plane, Arena* temp);

Brush* CreateBrush(Array<QuakeBrushFace> quakeFaces, Arena* storage) {
  Brush* brush = Allocate<Brush>(storage);
  brush->faces = CreateArray<Plane>(storage, quakeFaces.count);

  for (int i = 0; i < quakeFaces.count; i++) {
    brush->faces[i] = quakeFaces[i].plane;
  }

  return brush;
}

Geometry* CreateGeometry(const Brush* brush, Arena* storage) {
  // Create a huge cube the size of the world.
  AABB worldBounds;
  worldBounds.center = Vector3::Zero;
  worldBounds.halfExtents = Vector3::One * WORLD_SIZE;

  // We temporarily allocate some world geometry - we only need it 
  // to initialize the clipper.
  Arena::Mark mark = Mark(storage);
  Geometry* worldGeometry = CreateGeometry(worldBounds, storage);
  ClippingGeometry clippingGeometry = CreateClippingGeometry(worldGeometry);
  Reset(storage, mark);

  // Slice the huge world cube by each face of the brush,
  // until we evenutally get the (much smaller) final geometry.
  for (Plane face : brush->faces) {
    Clip(&clippingGeometry, face, storage);
  }

  // Convert the clipping geometry back into normal geometry.
  Geometry* geometry = CreateGeometry(&clippingGeometry, storage);
  FreeClippingGeometry(&clippingGeometry);
  return geometry;
}

Geometry* CreateGeometry(AABB bounds, Arena* arena) {
  // Big cube time
  Vector3 min = bounds.center - bounds.halfExtents;
  Vector3 max = bounds.center + bounds.halfExtents;

  Geometry* result = Allocate<Geometry>(arena);

  Array<GeometryVertex>& v = result->vertices;
  v = CreateArray<GeometryVertex>(arena, 8);
  v[0] = GeometryVertex{min.x, min.y, min.z}; // front bottom left
  v[1] = GeometryVertex{min.x, max.y, min.z}; // front top left
  v[2] = GeometryVertex{max.x, max.y, min.z}; // front top right
  v[3] = GeometryVertex{max.x, min.y, min.z}; // front bottom 
  v[4] = GeometryVertex{min.x, min.y, max.z}; // back bottom left
  v[5] = GeometryVertex{min.x, max.y, max.z}; // back top left
  v[6] = GeometryVertex{max.x, max.y, max.z}; // back top right
  v[7] = GeometryVertex{max.x, min.y, max.z}; // back bottom right

  Array<GeometryEdge>& e = result->edges;
  e = CreateArray<GeometryEdge>(arena, 12);
  e[0]  = GeometryEdge{{0, 3}, {0, 5}}; // front-bottom 
  e[1]  = GeometryEdge{{1, 2}, {0, 4}}; // front-top 
  e[2]  = GeometryEdge{{0, 1}, {0, 2}}; // front-left 
  e[3]  = GeometryEdge{{2, 3}, {0, 3}}; // front-right 
  e[4]  = GeometryEdge{{4, 7}, {1, 5}}; // back-bottom 
  e[5]  = GeometryEdge{{5, 6}, {1, 4}}; // back-top 
  e[6]  = GeometryEdge{{4, 5}, {1, 2}}; // back-left 
  e[7]  = GeometryEdge{{6, 7}, {1, 3}}; // back-right 
  e[8]  = GeometryEdge{{0, 4}, {5, 2}}; // side-bottom-left 
  e[9]  = GeometryEdge{{1, 5}, {4, 2}}; // side-top-left 
  e[10] = GeometryEdge{{3, 7}, {5, 3}}; // side-bottom-right
  e[11] = GeometryEdge{{2, 6}, {4, 3}}; // side-top-right 

  Array<GeometryFace>& f = result->faces;
  f = CreateArray<GeometryFace>(arena, 6);
  f[0] = GeometryFace{CreateArray(0, 1, 2, 3, arena),   { 0, 0,-1}}; // front
  f[1] = GeometryFace{CreateArray(4, 5, 6, 7, arena),   { 0, 0, 1}}; // back
  f[2] = GeometryFace{CreateArray(2, 6, 8, 9, arena),   {-1, 0, 0}}; // left
  f[3] = GeometryFace{CreateArray(3, 7, 10, 11, arena), { 1, 0, 0}}; // right
  f[4] = GeometryFace{CreateArray(1, 5, 9, 11, arena),  { 0, 1, 0}}; // top
  f[5] = GeometryFace{CreateArray(0, 4, 8, 10, arena),  { 0,-1, 0}}; // bottom

  return result;
}

static void Clip(ClippingGeometry* geometry, Plane plane, Arena* temp) {
  Arena::Mark mark = Mark(temp);

  // Used for floating point comparisons to avoid error with small values
  constexpr float EPSILON = 0.01f;

  int countClipped = 0;
  int countTotal = 0;

  // Step one: Calculate the distance of each vertex from the clipping plane.
  // If the vertex falls on the positive side of the clipping plane, we "clip" it
  // by making it invisible.
  float* distances = (float*)Allocate(temp, sizeof(float) * geometry->vertices.count);

  for (int i = 0; i < geometry->vertices.count; i++) {
    ClippingVertex& vertex = geometry->vertices[i];

    if (!vertex.isVisible) {
      continue;
    }

    countTotal++;
    distances[i] = Distance(vertex.position, plane);

    if (distances[i] >= EPSILON) {
      countClipped++;
      vertex.isVisible = false;
      geometry->visibleVertexCount--;
    } 
    else if (distances[i] >= -EPSILON) {
      // Snap the distance to 0 if it's really small.
      distances[i] = 0;
    }
  }

  bool nothingClipped = countClipped == 0;
  bool everythingClipped = countClipped == countTotal;

  // A couple easy edge cases that can save us some work
  if (nothingClipped || everythingClipped) {
    return; 
  }

  // Step two: Determine the visibility of each edge.
  for (int i = 0; i < geometry->edges.count; i++) {
    ClippingEdge& edge = geometry->edges[i];

    if (!edge.isVisible) {
      continue;
    }

    ClippingVertex& v0 = geometry->vertices[edge.vertexIndices[0]];
    ClippingVertex& v1 = geometry->vertices[edge.vertexIndices[1]];

    if (!v0.isVisible && !v1.isVisible) {
      // The edge lost both of it's vertices: it is completely clipped.
      edge.isVisible = false;
      geometry->visibleEdgeCount--;
      // Remember that faces also track their edges: need to
      // remove deleted edges from the faces as well.
      ClippingFace& f0 = geometry->faces[edge.faceIndices[0]];
      ClippingFace& f1 = geometry->faces[edge.faceIndices[1]];

      Remove(i, &f0.edgeIndices);
      Remove(i, &f1.edgeIndices);

      if (f0.edgeIndices.count == 0) {
        f0.isVisible = false;
        geometry->visibleFaceCount--;
      }

      if (f1.edgeIndices.count == 0) {
        f1.isVisible = false;
        geometry->visibleFaceCount--;
      }
    } 
    else if (v0.isVisible && v1.isVisible) {
      // The edge is fully visible: no need to do anything.
      continue;
    } 
    else {
      // The edge lost one of its two vertices: it is half-split.
      // Calculate the midpoint at which the edge is split.
      // The calculation of "t" can be visualized like this:
      //
      //        v0 = = = Plane = = = v1
      //        |----[d0]--|
      //                   |--[d1]----|
      //        |-------[d0-d1]-------|
      // 
      float d0 = distances[edge.vertexIndices[0]];
      float d1 = distances[edge.vertexIndices[1]];
      float t =  d0 / (d0 - d1);
      Vector3 position = Lerp(v0.position, v1.position, t);

      // Create a new visible vertex at the midpoint.
      // New edges to connect the new vertices are created later,
      // during face processing.
      int newVertexIndex = geometry->vertices.count;
      ClippingVertex newVertex;
      newVertex.position = position;
      newVertex.isVisible = true;
      geometry->visibleVertexCount++;
      PushBack(newVertex, &geometry->vertices);

      // Replace whichever vertex was clipped in this edge
      // with the new one.
      edge.vertexIndices[v0.isVisible ? 1 : 0] = newVertexIndex;
    }
  }

  // Step three: Create new edges to connect the newly 
  // created vertices into the existing faces. 
  // We also create a new face to close the created hole.
  int newFaceIndex = geometry->faces.count;
  ClippingFace newFace;
  newFace.edgeIndices = CreateDynamicList<int>(3);
  newFace.isVisible = true;
  geometry->visibleFaceCount++;
  newFace.normal = plane.normal;

  for (int faceIndex = 0; faceIndex < geometry->faces.count; faceIndex++) {
    ClippingFace &face = geometry->faces[faceIndex];

    if (!face.isVisible) {
      continue;
    }

    // Determine if the face is missing an edge.
    // This problem is solved by counting how many times each vertex occurs in an edge.
    // In a closed loop, each vertex occurs exactly twice (once in each edge it connects to).
    // In an open loop, there will be some vertices that only occur once.
    int* occurs = (int*)Allocate(temp, sizeof(int) * geometry->vertices.count);

    for (int i = 0; i < face.edgeIndices.count; i++) {
      ClippingEdge &edge = geometry->edges.data[face.edgeIndices.data[i]];
      occurs[edge.vertexIndices[0]]++;
      occurs[edge.vertexIndices[1]]++;
    }

    // A vertex that only occurs once is an "endpoint".
    // If there are only two endpoints, and we connect them with a new edge, we have closed the loop.
    int endpointIndices[2] = {-1, -1};

    for (int i = 0; i < face.edgeIndices.count; i++) {
      ClippingEdge &edge = geometry->edges.data[face.edgeIndices.data[i]];

      int endpointIndex = -1;
      if (occurs[edge.vertexIndices[0]] == 1) endpointIndex = edge.vertexIndices[0];
      if (occurs[edge.vertexIndices[1]] == 1) endpointIndex = edge.vertexIndices[1];

      // We can ignore this edge if it does not contain an endpoint
      if (endpointIndex == -1) {
        continue;
      }

      // Scan for an available endpoint (one that is still defaulted to -1).
      int availableEndpoint = -1;

      for (int i = 0; i < 2; i++) {
        if (endpointIndices[i] == -1) {
          availableEndpoint = i;
          break;
        }
      }

      // In this case, there must be more than one
      // missing edges, which should be impossible
      // when clipping by a single plane.
      assert(availableEndpoint != -1);

      endpointIndices[availableEndpoint] = endpointIndex;
    }

    if (endpointIndices[0] != -1 && endpointIndices[1] != -1) {
      int newEdgeIndex = geometry->edges.count;
      ClippingEdge newEdge;
      newEdge.isVisible = true;
      geometry->visibleEdgeCount++;
      newEdge.vertexIndices[0] = endpointIndices[0];
      newEdge.vertexIndices[1] = endpointIndices[1];
      newEdge.faceIndices[0] = faceIndex;
      newEdge.faceIndices[1] = newFaceIndex;
      PushBack(newEdge, &geometry->edges);

      // Remember to update the faces that this edge is connected to.
      PushBack(newEdgeIndex, &face.edgeIndices);
      PushBack(newEdgeIndex, &newFace.edgeIndices);
    }
  }

  PushBack(newFace, &geometry->faces);
  Reset(temp, mark);
}

static ClippingGeometry CreateClippingGeometry(const Geometry* geometry) {
  // Clipping geometry is very similar to normal geometry - in fact,
  // most of the "ClippingXxx" structs inherit from a "GeometryXxx" struct.

  // This making initialization very easy - we copy geometry data
  // directly into the clipping data, and initialize the remaining
  // values (setting everything to visible).

  ClippingGeometry result;

  // Process vertices
  result.vertices = CreateDynamicList<ClippingVertex>(geometry->vertices.count);
  result.vertices.count = geometry->vertices.count;
  result.visibleVertexCount = geometry->vertices.count;

  for (int i = 0; i < result.vertices.count; i++) {
    MemoryCopy(&result.vertices[i], &geometry->vertices[i], sizeof(GeometryVertex));
    result.vertices[i].isVisible = true;
  }

  // Process edges
  result.edges = CreateDynamicList<ClippingEdge>(geometry->edges.count);
  result.edges.count = geometry->edges.count;
  result.visibleEdgeCount = geometry->edges.count;

  for (int i = 0; i < result.edges.count; i++) {
    MemoryCopy(&result.edges[i], &geometry->edges[i], sizeof(GeometryEdge));
    result.edges[i].isVisible = true;
  }

  // Process faces
  result.faces = CreateDynamicList<ClippingFace>(geometry->faces.count);
  result.faces.count = geometry->faces.count;
  result.visibleFaceCount = geometry->faces.count;

  for (int i = 0; i < result.faces.count; i++) {
    // Since ClippingFace is unique and does not extend GeometryFace,
    // we manually copy the data we need.
    result.faces[i].edgeIndices = CreateDynamicList<int>(&geometry->faces[i].edgeIndices);
    result.faces[i].normal = geometry->faces[i].normal;
    result.faces[i].isVisible = true;
  }

  return result;
}

static void FreeClippingGeometry(ClippingGeometry* geometry) {
  FreeDynamicList(&geometry->edges);
  FreeDynamicList(&geometry->vertices);

  for (ClippingFace& face : geometry->faces) {
    FreeDynamicList(&face.edgeIndices);
  }

  FreeDynamicList(&geometry->faces);
}

static Geometry* CreateGeometry(ClippingGeometry* geometry, Arena* storage) {
  // Transforming clipping geometry back into normal geometry is complicated
  // since the clipping geometry is still full of invisible elements.

  // We need to identify and ignore these elements, thus creating a dense
  // list of visible geometry. 

  // However, many geometry elements contain references to each other; 
  // for example, edges know the index of their connected vertices and planes.
  // When we compact the arrays by removing invisible geometry, this will
  // invalidate these references. To address this, we create temporary arrays
  // that map the old invalid indices to the newer ones.

  FixedList<GeometryVertex> vertices = CreateFixedList<GeometryVertex>(storage, geometry->visibleVertexCount);
  FixedList<GeometryEdge> edges = CreateFixedList<GeometryEdge>(storage, geometry->visibleEdgeCount);
  FixedList<GeometryFace> faces = CreateFixedList<GeometryFace>(storage, geometry->visibleFaceCount);

  Geometry* result = Allocate<Geometry>(storage);
  result->vertices = CreateArray(vertices);
  result->edges = CreateArray(edges);
  result->faces = CreateArray(faces);

  Arena::Mark mark = Mark(storage);
  Array<int> vertexMap = CreateArray<int>(storage, geometry->vertices.count);
  Array<int> edgeMap = CreateArray<int>(storage, geometry->edges.count);
  Array<int> faceMap = CreateArray<int>(storage, geometry->faces.count);

  // Process vertices
  for (int i = 0; i < geometry->vertices.count; i++) {
    if (geometry->vertices[i].isVisible) {
      GeometryVertex vertex;
      MemoryCopy(&vertex, &geometry->vertices[i], sizeof(GeometryVertex));

      PushBack(vertex, &vertices);

      // Update our fix-up map with this new vertex
      int newIndex = result->vertices.count++;
      vertexMap[i] = newIndex;
    }
  }


  // Process edges
  result->edges.count = 0;
  result->edges.data = (GeometryEdge*)Allocate(storage, 0);

  for (int i = 0; i < geometry->edges.count; i++) {
    if (geometry->edges[i].isVisible) {
      GeometryEdge edge;
      MemoryCopy(&edge, &geometry->edges[i], sizeof(GeometryEdge));

      // Fix-up old vertex indices
      edge.vertexIndices[0] = vertexMap[edge.vertexIndices[0]];
      edge.vertexIndices[1] = vertexMap[edge.vertexIndices[1]];

      PushBack(edge, &edges);

      // Still need to initialize the face indices here,
      // but the faces have not been processed yet. 
      // We do this in a later loop, after the faces.

      // Update our fix-up map with this new edge
      int newIndex = result->edges.count++;
      edgeMap[i] = newIndex;
    }
  }

  // Process faces
  result->faces.count = 0;
  result->faces.data = (GeometryFace*)Allocate(storage, 0);

  for (int i = 0; i < geometry->faces.count; i++) {
    if (geometry->faces[i].isVisible) {
      GeometryFace face;
      MemoryCopy(&face, &geometry->faces[i], sizeof(GeometryFace));

      // Fix-up old edge indices
      face.edgeIndices[0] = edgeMap[face.edgeIndices[0]];
      face.edgeIndices[1] = edgeMap[face.edgeIndices[1]];

      PushBack(face, &faces);

      // Update our fix-up map with this new face
      int newIndex = result->faces.count++;
      faceMap[i] = newIndex;
    }
  }

  // Fix-up face indices for edges
  for (GeometryEdge& edge : result->edges) {
    edge.faceIndices[0] = faceMap[edge.faceIndices[0]];
    edge.faceIndices[1] = faceMap[edge.faceIndices[1]];
  }

  // Free all of the temporary fix-up maps
  Reset(storage, mark);
  return result;
}

// A helper to make array initialization more readable and terse
static Array<int> CreateArray(int a, int b, int c, int d, Arena* storage) {
  Array<int> result = CreateArray<int>(storage, 4);
  result[0] = a;
  result[1] = b;
  result[2] = c;
  result[3] = d;
  return result;
}