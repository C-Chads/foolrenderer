// Copyright (c) Caden Ji. All rights reserved.
//
// Licensed under the MIT License. See LICENSE file in the project root for
// license information.

#include "utilities/mesh.h"

#include <fast_obj.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "math/vector.h"

#define VERTEX_EQUAL(a, b) \
    ((a)->p == (b)->p && (a)->t == (b)->t && (a)->n == (b)->n)

// Put vertex at the end of vertex_set and return its index in vertex_set, If
// the vertex has been put into vertex_set, the index is returned directly.
static uint32_t put_vertex(fastObjIndex vertex_set[], uint32_t *vertex_set_size,
                           const fastObjIndex *vertex) {
    // The current solution is stupid but simple to implement...
    // This function can be optimized using a hash map.
    for (uint32_t i = 0; i < *(vertex_set_size); i++) {
        fastObjIndex *v = vertex_set + i;
        if (VERTEX_EQUAL(v, vertex)) {
            return i;
        }
    }
    vertex_set[*vertex_set_size] = *vertex;
    ++(*vertex_set_size);
    return *vertex_set_size - 1;
}

// Returns true if failed, otherwise returns false.
static bool set_vertex_attributes(struct mesh *mesh, const fastObjMesh *data) {
    uint32_t index_count = 0;
    for (unsigned int f = 0; f < data->face_count; f++) {
        unsigned int face_vertices = data->face_vertices[f];
        if (face_vertices != 0 && face_vertices != 3) {
            // Failed to load mesh if mesh contains non-triangular faces. Faces
            // with 0 vertices can be ignored directly.
            return true;
        }
        index_count += face_vertices;
    }

    uint32_t *indices = malloc(sizeof(uint32_t) * index_count);
    if (indices == NULL) {
        return true;
    }

    fastObjIndex vertex_set[index_count];
    uint32_t vertex_set_size = 0;
    // Check if the model file contains texcoord or normal data as they are
    // optional.
    bool has_texcoords = false;
    bool has_normals = false;

    for (uint32_t i = 0; i < index_count; i++) {
        const fastObjIndex *vertex = data->indices + i;
        uint32_t vertex_set_index =
            put_vertex(vertex_set, &vertex_set_size, vertex);
        indices[i] = vertex_set_index;
        // A mesh is considered to contain texcoord or normal data as long as
        // one vertex contains a valid texcoord or normal index.
        unsigned int ti = vertex->t;
        has_texcoords = has_texcoords || (ti > 0 && ti < data->texcoord_count);
        unsigned int ni = vertex->n;
        has_normals = has_normals || (ni > 0 && ni < data->normal_count);
    }

    vector3 *positions = malloc(sizeof(vector3) * vertex_set_size);
    vector2 *texcoords =
        has_texcoords ? malloc(sizeof(vector2) * vertex_set_size) : NULL;
    vector3 *normals =
        has_normals ? malloc(sizeof(vector3) * vertex_set_size) : NULL;
    if (positions == NULL || (has_texcoords && texcoords == NULL) ||
        (has_normals && normals == NULL)) {
        free(indices);
        free(positions);
        free(texcoords);
        free(normals);
        return true;
    }

    for (uint32_t i = 0; i < vertex_set_size; i++) {
        float *src;
        fastObjIndex *vertex = vertex_set + i;

        // Set positions.
        if (vertex->p < data->position_count) {
            src = data->positions + vertex->p * 3;
        } else {
            // index out of bounds, use dummy data at index 0.
            src = data->positions;
        }
        memcpy((positions + i)->elements, data->positions + vertex->p * 3,
               sizeof(float) * 3);
        // Set texcoords.
        if (has_texcoords) {
            if (vertex->t < data->texcoord_count) {
                src = data->texcoords + vertex->t * 2;
            } else {
                src = data->texcoords;
            }
            memcpy((texcoords + i)->elements, data->texcoords + vertex->t * 2,
                   sizeof(float) * 2);
        }
        // Set normals.
        if (has_normals) {
            if (vertex->n < data->normal_count) {
                src = data->normals + vertex->n * 3;
            } else {
                src = data->normals;
            }
            memcpy((normals + i)->elements, data->normals + vertex->n * 3,
                   sizeof(float) * 3);
            // Normal data in .obj files may not be normalized.
            normals[i] = vector3_normalize(normals[i]);
        }
    }

    mesh->positions = positions;
    mesh->texcoords = texcoords;
    mesh->normals = normals;
    mesh->indices = indices;
    mesh->vertex_count = vertex_set_size;
    mesh->triangle_count = index_count / 3;
    return false;
}

// Returns true if failed, otherwise returns false.
static bool set_diffuse_texture_name(struct mesh *mesh,
                                     const fastObjMesh *data) {
    mesh->diffuse_texture_path = NULL;
    if (data->material_count == 0) {
        return false;
    }
    char *texture_path = data->materials->map_Kd.path;
    if (texture_path == NULL) {
        return false;
    }
    size_t length = strlen(texture_path) + 1;
    if (length <= 1) {
        return false;
    }
    mesh->diffuse_texture_path = malloc(length);
    if (mesh->diffuse_texture_path == NULL) {
        return true;
    }
    strncpy(mesh->diffuse_texture_path, texture_path, length);
    return false;
}

// Calculates the average unit-length normal vector for each vertex in the mesh.
//
// Returns true if failed, otherwise returns false.
static bool compute_normals(struct mesh *mesh) {
    mesh->normals = malloc(sizeof(vector3) * mesh->vertex_count);
    if (mesh->normals == NULL) {
        return true;
    }

    for (uint32_t v = 0; v < mesh->vertex_count; v++) {
        mesh->normals[v] = VECTOR3_ZERO;
    }
    for (uint32_t t = 0; t < mesh->triangle_count; t++) {
        // For calculating surface normals, refer to:
        // https://www.khronos.org/opengl/wiki/Calculating_a_Surface_Normal
        uint32_t index_0 = mesh->indices[t * 3];
        uint32_t index_1 = mesh->indices[t * 3 + 1];
        uint32_t index_2 = mesh->indices[t * 3 + 2];
        const vector3 *p0 = mesh->positions + index_0;
        const vector3 *p1 = mesh->positions + index_1;
        const vector3 *p2 = mesh->positions + index_2;
        vector3 u = vector3_subtract(*p1, *p0);
        vector3 v = vector3_subtract(*p2, *p0);
        // Vertices are stored in counterclockwise order by default in .obj
        // files. And the foolrenderer uses a right-handed coordinate system. So
        // use "n = u X v" to calculate the normal.
        vector3 n = vector3_cross(u, v);
        // Add the surface normal of the triangle to the normals already present
        // on the three vertices of the triangle. Note that the triangle surface
        // normal is not normalized, its magnitude is twice the area of the
        // triangle, so that the normal direction of a triangle with a larger
        // area has a larger contribution to the normal direction of adjacent
        // vertices.
        mesh->normals[index_0] = vector3_add(mesh->normals[index_0], n);
        mesh->normals[index_1] = vector3_add(mesh->normals[index_1], n);
        mesh->normals[index_2] = vector3_add(mesh->normals[index_2], n);
    }
    // Normalize the normals of all vertices to get the average result.
    for (uint32_t v = 0; v < mesh->vertex_count; v++) {
        mesh->normals[v] = vector3_normalize(mesh->normals[v]);
    }
    return false;
}

// Calculates the average unit-length tangent vector for each vertex of the mesh
// from the normals and texcoords. If the mesh does not have normals or
// texcoords, the mesh's tangents will point to a null pointer.
//
// Returns true if failed, otherwise returns false.
static bool compute_tangents(struct mesh *mesh) {
    if (mesh->normals == NULL || mesh->texcoords == NULL) {
        mesh->tangents = NULL;
        return false;
    }
    mesh->tangents = malloc(sizeof(vector4) * mesh->vertex_count);
    if (mesh->tangents == NULL) {
        return true;
    }

    // Temporary array for tangents and bitangents and initialize to zero
    // vector.
    vector3 tangents[mesh->vertex_count];
    vector3 bitangents[mesh->vertex_count];
    for (uint32_t v = 0; v < mesh->vertex_count; v++) {
        tangents[v] = VECTOR3_ZERO;
        bitangents[v] = VECTOR3_ZERO;
    }
    // This function use Lengyel’s method, for more details refer to:
    // http://www.terathon.com/code/tangent.html
    for (uint32_t t = 0; t < mesh->triangle_count; t++) {
        uint32_t index_0 = mesh->indices[t * 3];
        uint32_t index_1 = mesh->indices[t * 3 + 1];
        uint32_t index_2 = mesh->indices[t * 3 + 2];
        const vector3 *p0 = mesh->positions + index_0;
        const vector3 *p1 = mesh->positions + index_1;
        const vector3 *p2 = mesh->positions + index_2;
        const vector2 *w0 = mesh->texcoords + index_0;
        const vector2 *w1 = mesh->texcoords + index_1;
        const vector2 *w2 = mesh->texcoords + index_2;

        vector3 e1 = vector3_subtract(*p1, *p0);
        vector3 e2 = vector3_subtract(*p2, *p0);
        float x1 = w1->u - w0->u;
        float x2 = w2->u - w0->u;
        float y1 = w1->v - w0->v;
        float y2 = w2->v - w0->v;

        float d = x1 * y2 - x2 * y1;
        vector3 tangent, bitangent;
        if (d == 0.0f) {
            tangent = VECTOR3_ZERO;
            bitangent = VECTOR3_ZERO;
        } else {
            float r = 1.0f / d;
            tangent = vector3_subtract(vector3_multiply_scalar(e1, y2),
                                       vector3_multiply_scalar(e2, y1));
            tangent = vector3_multiply_scalar(tangent, r);
            bitangent = vector3_subtract(vector3_multiply_scalar(e2, x1),
                                         vector3_multiply_scalar(e1, x2));
            bitangent = vector3_multiply_scalar(bitangent, r);
        }
        tangents[index_0] = vector3_add(tangents[index_0], tangent);
        tangents[index_1] = vector3_add(tangents[index_1], tangent);
        tangents[index_2] = vector3_add(tangents[index_2], tangent);
        bitangents[index_0] = vector3_add(bitangents[index_0], bitangent);
        bitangents[index_1] = vector3_add(bitangents[index_1], bitangent);
        bitangents[index_2] = vector3_add(bitangents[index_2], bitangent);
    }

    for (uint32_t v = 0; v < mesh->vertex_count; v++) {
        vector3 *t = tangents + v;
        const vector3 *b = bitangents + v;
        const vector3 *n = mesh->normals + v;
        // Gram-Schmidt orthogonalize.
        *t = vector3_subtract(*t,
                              vector3_multiply_scalar(*n, vector3_dot(*n, *t)));
        *t = vector3_normalize(*t);

        vector4 *tangent = mesh->tangents + v;
        tangent->x = t->x;
        tangent->y = t->y;
        tangent->z = t->z;
        tangent->w =
            vector3_dot(vector3_cross(*n, *t), *b) < 0.0f ? -1.0f : 1.0f;
    }
    return false;
}

struct mesh *load_mesh(const char *filename) {
    struct mesh *mesh;
    mesh = malloc(sizeof(struct mesh));
    if (mesh == NULL) {
        return NULL;
    }

    fastObjMesh *data = fast_obj_read(filename);
    if (data == NULL) {
        goto load_failed;
    }
    if (set_vertex_attributes(mesh, data)) {
        goto load_failed;
    }
    if (set_diffuse_texture_name(mesh, data)) {
        goto load_failed;
    }
    if (mesh->normals == NULL) {
        if (compute_normals(mesh)) {
            goto load_failed;
        }
    }
    if (compute_tangents(mesh)) {
        goto load_failed;
    }

    fast_obj_destroy(data);
    return mesh;

load_failed:
    destroy_mesh(mesh);
    if (data != NULL) {
        fast_obj_destroy(data);
    }
    return NULL;
}

void destroy_mesh(struct mesh *mesh) {
    if (mesh != NULL) {
        free(mesh->positions);
        free(mesh->texcoords);
        free(mesh->normals);
        free(mesh->tangents);
        free(mesh->indices);
        free(mesh->diffuse_texture_path);
        free(mesh);
    }
}

void get_mesh_position(vector3 *position, const struct mesh *mesh,
                       uint32_t triangle_index, uint32_t vertex_index) {
    if (triangle_index >= mesh->triangle_count || vertex_index > 2) {
        *position = VECTOR3_ZERO;
    } else {
        uint32_t index = mesh->indices[triangle_index * 3 + vertex_index];
        *position = mesh->positions[index];
    }
}

void get_mesh_texcoord(vector2 *texcoord, const struct mesh *mesh,
                       uint32_t triangle_index, uint32_t vertex_index) {
    if (triangle_index >= mesh->triangle_count || vertex_index > 2 ||
        mesh->texcoords == NULL) {
        *texcoord = VECTOR2_ZERO;
    } else {
        uint32_t index = mesh->indices[triangle_index * 3 + vertex_index];
        *texcoord = mesh->texcoords[index];
    }
}

void get_mesh_normal(vector3 *normal, const struct mesh *mesh,
                     uint32_t triangle_index, uint32_t vertex_index) {
    if (triangle_index >= mesh->triangle_count || vertex_index > 2 ||
        mesh->normals == NULL) {
        *normal = VECTOR3_ZERO;
    } else {
        uint32_t index = mesh->indices[triangle_index * 3 + vertex_index];
        *normal = mesh->normals[index];
    }
}

void get_mesh_tangent(vector4 *tangent, const struct mesh *mesh,
                      uint32_t triangle_index, uint32_t vertex_index) {
    if (triangle_index >= mesh->triangle_count || vertex_index > 2 ||
        mesh->tangents == NULL) {
        *tangent = VECTOR4_ZERO;
    } else {
        uint32_t index = mesh->indices[triangle_index * 3 + vertex_index];
        *tangent = mesh->tangents[index];
    }
}
