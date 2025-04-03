#pragma once

#include "vector3.hpp"
#include "array.hpp"

struct GeometryVertex {
  Vector3 position;
};

struct GeometryFace;
struct GeometryVertex;

struct GeometryEdge {
  int vertexIndices[2];
  int faceIndices[2];
};

struct GeometryFace {
  Array<int> edgeIndices;
  Vector3 normal;
};

struct Geometry {
  Array<GeometryVertex> vertices;
  Array<GeometryEdge> edges;
  Array<GeometryFace> faces;
};