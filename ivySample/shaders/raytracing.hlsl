// This file is part of the AMD Work Graph Ivy Generation Sample.
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

RaytracingAccelerationStructure Tlas : register(t0);

StructuredBuffer<Material_Info> g_material_info : DECLARE_SRV(RAYTRACING_INFO_MATERIAL);
StructuredBuffer<Instance_Info> g_instance_info : DECLARE_SRV(RAYTRACING_INFO_INSTANCE);
StructuredBuffer<uint>          g_surface_id : DECLARE_SRV(RAYTRACING_INFO_SURFACE_ID);
StructuredBuffer<Surface_Info>  g_surface_info : DECLARE_SRV(RAYTRACING_INFO_SURFACE);

Texture2D g_textures[MAX_TEXTURES_COUNT] : DECLARE_SRV(TEXTURE_BEGIN_SLOT);
SamplerState g_samplers[MAX_SAMPLERS_COUNT] : DECLARE_SAMPLER(SAMPLER_BEGIN_SLOT);

StructuredBuffer<uint>  g_index_buffer[MAX_BUFFER_COUNT] : DECLARE_SRV(INDEX_BUFFER_BEGIN_SLOT);
StructuredBuffer<float> g_vertex_buffer[MAX_BUFFER_COUNT] : DECLARE_SRV(VERTEX_BUFFER_BEGIN_SLOT);

uint3 FetchIndicesU32(in uint offset, in uint triangle_id)
{
    return uint3(g_index_buffer[NonUniformResourceIndex(offset)].Load(3 * triangle_id),
                 g_index_buffer[NonUniformResourceIndex(offset)].Load(3 * triangle_id + 1),
                 g_index_buffer[NonUniformResourceIndex(offset)].Load(3 * triangle_id + 2));
}

uint3 FetchIndicesU16(in uint offset, in uint triangle_id)
{
    uint word_id_0  = triangle_id * 3 + 0;
    uint dword_id_0 = word_id_0 / 2;
    uint shift_0    = 16 * (word_id_0 & 1);

    uint word_id_1  = triangle_id * 3 + 1;
    uint dword_id_1 = word_id_1 / 2;
    uint shift_1    = 16 * (word_id_1 & 1);

    uint word_id_2  = triangle_id * 3 + 2;
    uint dword_id_2 = word_id_2 / 2;
    uint shift_2    = 16 * (word_id_2 & 1);

    uint u0 = g_index_buffer[NonUniformResourceIndex(offset)].Load(dword_id_0);
    u0      = (u0 >> shift_0) & 0xffffu;
    uint u1 = g_index_buffer[NonUniformResourceIndex(offset)].Load(dword_id_1);
    u1      = (u1 >> shift_1) & 0xffffu;
    uint u2 = g_index_buffer[NonUniformResourceIndex(offset)].Load(dword_id_2);
    u2      = (u2 >> shift_0) & 0xffffu;

    return uint3(u0, u1, u2);
}

float2 FetchFloat2(in int offset, in int vertex_id)
{
    float2 data;
    data[0] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(2 * vertex_id);
    data[1] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(2 * vertex_id + 1);

    return data;
}
float3 FetchFloat3(in int offset, in int vertex_id)
{
    float3 data;
    data[0] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(3 * vertex_id);
    data[1] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(3 * vertex_id + 1);
    data[2] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(3 * vertex_id + 2);

    return data;
}
float4 FetchFloat4(in int offset, in int vertex_id)
{
    float4 data;
    data[0] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(4 * vertex_id);
    data[1] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(4 * vertex_id + 1);
    data[2] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(4 * vertex_id + 2);
    data[3] = g_vertex_buffer[NonUniformResourceIndex(offset)].Load(4 * vertex_id + 3);

    return data;
}

float3 FetchNormal(in Surface_Info sinfo, in uint3 face3, in float2 bary)
{
    float3 normal0 = FetchFloat3(sinfo.normal_attribute_offset, face3.x);
    float3 normal1 = FetchFloat3(sinfo.normal_attribute_offset, face3.y);
    float3 normal2 = FetchFloat3(sinfo.normal_attribute_offset, face3.z);
    return normal1 * bary.x + normal2 * bary.y + normal0 * (1.0 - bary.x - bary.y);
}


bool TraceRay(in float3 origin, 
              in float3 direction,
              in float tMin,
              in float tMax,
              out float3 hitPosition,
              out float3 hitNormal)
{
    RayDesc ray;

    ray.Origin = origin;    
    ray.TMin = tMin;
    ray.Direction = direction;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(Tlas, 0, 0xFF, ray);
    q.Proceed();
        
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        hitPosition = ray.Origin + ray.Direction * ray.TMax;
        hitNormal = float3(0, 1, 0);

        return false;
    }
        
    hitPosition = ray.Origin + ray.Direction * q.CommittedRayT();

    const uint instance_id  = q.CommittedInstanceID();
    const uint geometry_id  = q.CommittedGeometryIndex();

    const Instance_Info iinfo      = g_instance_info.Load(instance_id);
    const uint          surface_id = g_surface_id.Load((iinfo.surface_id_table_offset + geometry_id));
    const Surface_Info  sinfo      = g_surface_info.Load(surface_id);

    const uint  triangleId = q.CommittedPrimitiveIndex();
    const uint3 indices =
        (sinfo.index_type == SURFACE_INFO_INDEX_TYPE_U16) ? FetchIndicesU16(sinfo.index_offset, triangleId) : FetchIndicesU32(sinfo.index_offset, triangleId);

    const float3 normal = FetchNormal(sinfo, indices, q.CommittedTriangleBarycentrics());

    hitNormal = normalize(mul((float3x3)q.CommittedObjectToWorld3x4(), normal));

    return true;
}