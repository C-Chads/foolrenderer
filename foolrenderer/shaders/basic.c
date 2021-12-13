// Copyright (c) 2021 Caden Ji
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "shaders/basic.h"

#include <math.h>

#include "math/math_utility.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "shader_context.h"
#include "texture.h"

#define TEXCOORD 0
#define NORMAL 0
#define POSITION 1

vector4 basic_vertex_shader(struct shader_context *output, const void *uniform,
                            const void *vertex_attribute) {
    const struct basic_uniform *unif = uniform;
    const struct basic_vertex_attribute *attr = vertex_attribute;

    vector2 *out_texcoord = shader_context_vector2(output, TEXCOORD);
    *out_texcoord = attr->texcoord;

    vector4 normal_in_view = matrix4x4_multiply_vector4(
        unif->normal_matrix, vector3_to_4(attr->normal, 0.0f));
    vector3 *out_normal = shader_context_vector3(output, NORMAL);
    *out_normal = vector4_to_3(normal_in_view);

    vector4 position_in_view = matrix4x4_multiply_vector4(
        unif->modelview, vector3_to_4(attr->position, 1.0f));
    vector3 *out_position = shader_context_vector3(output, POSITION);
    *out_position = vector4_to_3(position_in_view);

    return matrix4x4_multiply_vector4(unif->projection, position_in_view);
}

vector4 basic_fragment_shader(struct shader_context *input,
                              const void *uniform) {
    const struct basic_uniform *unif = uniform;

    vector3 *in_normal = shader_context_vector3(input, NORMAL);
    vector3 normal = vector3_normalize(*in_normal);

    // Ambient lighting.
    vector3 ambient_lighting =
        vector3_multiply(unif->ambient_color, unif->ambient_reflectance);

    // Diffuse lighting.
    float n_dot_l = vector3_dot(normal, unif->light_direction);
    float diffuse_intensity = max_float(0.0f, n_dot_l);
    vector3 diffuse_lighting =
        vector3_multiply_scalar(unif->light_color, diffuse_intensity);
    diffuse_lighting =
        vector3_multiply(diffuse_lighting, unif->diffuse_reflectance);

    // Specular lighting.
    vector3 specular_lighting = VECTOR3_ZERO;
    if (n_dot_l > 0.0f) {
        // Because in view space, the camera is at the origin, so the
        // calculation of
        // the view direction is simplified.
        vector3 *postion = shader_context_vector3(input, POSITION);
        vector3 view_direction = vector3_multiply_scalar(*postion, -1.0f);
        view_direction = vector3_normalize(view_direction);
        // Calculate the halfway vector between the light direction and the view
        // direction.
        vector3 halfway = vector3_add(view_direction, unif->light_direction);
        halfway = vector3_normalize(halfway);
        float n_dot_h = vector3_dot(normal, halfway);
        float specular_intensity =
            powf(max_float(0.0f, n_dot_h), unif->shininess);
        specular_lighting =
            vector3_multiply_scalar(unif->light_color, specular_intensity);
        specular_lighting =
            vector3_multiply(specular_lighting, unif->specular_reflectance);
    }

    vector2 *texcoord = shader_context_vector2(input, TEXCOORD);
    vector4 texture_color = VECTOR4_ONE;
    texture_sample(&texture_color, unif->diffuse_texture, *texcoord);

    vector3 fragment_color = vector3_add(ambient_lighting, diffuse_lighting);
    fragment_color =
        vector3_multiply(fragment_color, vector4_to_3(texture_color));
    fragment_color = vector3_add(fragment_color, specular_lighting);
    return vector3_to_4(fragment_color, 1.0f);
}
