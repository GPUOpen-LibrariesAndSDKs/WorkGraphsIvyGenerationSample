// This file is part of the AMD Work Graph Mesh Node Sample.
//
// Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#if __cplusplus
#include "misc/math.h"
#endif  // __cplusplus

#if __cplusplus
struct WorkGraphCBData
{
    Mat4 ViewProjection;
    Mat4 PreviousViewProjection;
    Mat4 InverseViewProjection;
    Vec4 CameraPosition;
    Vec4 PreviousCameraPosition;
    int  IvyStemSurfaceIndex;
    int  IvyLeafSurfaceIndex;
};
#else
cbuffer WorkGraphCBData : register(b0)
{
    matrix ViewProjection;
    matrix PreviousViewProjection;
    matrix InverseViewProjection;
    float4 CameraPosition;
    float4 PreviousCameraPosition;
    int    IvyStemSurfaceIndex;
    int    IvyLeafSurfaceIndex;
}
#endif  // __cplusplus

// Entry node records
struct IvyBranchRecord
{
#if __cplusplus
    Mat4         transform;
    unsigned int seed;
#else
    float4x4     transform;
    unsigned int seed;
#endif  // __cplusplus
};

struct IvyAreaRecord
{
#if __cplusplus
    Mat4         transform;
    unsigned int seed;
    float        density;
#else
    float4x4     transform;
    unsigned int seed;
    float        density;
#endif  // __cplusplus
};

#define MAX_TEXTURES_COUNT 1000
#define MAX_SAMPLERS_COUNT 20

#define RAYTRACING_INFO_BEGIN_SLOT 20
#define RAYTRACING_INFO_MATERIAL   20
#define RAYTRACING_INFO_INSTANCE   21
#define RAYTRACING_INFO_SURFACE_ID 22
#define RAYTRACING_INFO_SURFACE    23

#define TEXTURE_BEGIN_SLOT 50
#define SAMPLER_BEGIN_SLOT    10

#define INDEX_BUFFER_BEGIN_SLOT  1050
#define VERTEX_BUFFER_BEGIN_SLOT 21050

#define MAX_BUFFER_COUNT 20000

#define DECLARE_SRV_REGISTER(regIndex)     t##regIndex
#define DECLARE_SAMPLER_REGISTER(regIndex) s##regIndex

#define DECLARE_SRV(regIndex)     register(DECLARE_SRV_REGISTER(regIndex))
#define DECLARE_SAMPLER(regIndex) register(DECLARE_SAMPLER_REGISTER(regIndex))

#define SURFACE_INFO_INDEX_TYPE_U32 0
#define SURFACE_INFO_INDEX_TYPE_U16 1

struct Material_Info
{
    float albedo_factor_x;
    float albedo_factor_y;
    float albedo_factor_z;
    float albedo_factor_w;

    // A.R.M. Packed texture - Ambient occlusion | Roughness | Metalness
    float arm_factor_x;
    float arm_factor_y;
    float arm_factor_z;
    int   arm_tex_id;
    int   arm_tex_sampler_id;

    float emission_factor_x;
    float emission_factor_y;
    float emission_factor_z;
    int   emission_tex_id;
    int   emission_tex_sampler_id;

    int   normal_tex_id;
    int   normal_tex_sampler_id;
    int   albedo_tex_id;
    int   albedo_tex_sampler_id;
    float alpha_cutoff;
    int   is_opaque;
};

struct Instance_Info
{
    int surface_id_table_offset;
    int num_opaque_surfaces;
    int node_id;
    int num_surfaces;
};

struct Surface_Info
{
    int material_id;
    int index_offset;  // Offset for the first index
    int index_type;    // 0 - u32, 1 - u16
    int position_attribute_offset;

    int texcoord0_attribute_offset;
    int texcoord1_attribute_offset;
    int normal_attribute_offset;
    int tangent_attribute_offset;

    int num_indices;
    int num_vertices;
    int weight_attribute_offset;
    int joints_attribute_offset;
};