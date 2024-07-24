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

#include "common.hlsl"
#include "raytracing.hlsl"

// Vertex output struct for mesh shader
struct VertexOutputAttributes {
    float4 clipSpacePosition : SV_Position;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 texCoord : TEXCOORD0;
    float2 clipSpaceMotion : TEXCOORD1;
    int    materialId : BLENDINDICES0;
};

static const uint threadGroupSize      = 128;
static const uint numOutputVertices    = 134;
static const uint numOutputTriangles   = 124;

static const int numOutputVertexIterations   = (numOutputVertices + (threadGroupSize - 1)) / threadGroupSize;
static const int numOutputTriangleIterations = (numOutputTriangles + (threadGroupSize - 1)) / threadGroupSize;

[Shader("node")]
[NodeLaunch("mesh")]
[NodeId("DrawIvyStem", 0)]
[NodeMaxDispatchGrid(maxStemsPerRecord, 1, 1)]
[NumThreads(threadGroupSize, 1, 1)]
[OutputTopology("triangle")]
void IvyStemMeshShader(
    uint threadIndex : SV_GroupThreadId,
    uint groupIndex : SV_GroupId,
    DispatchNodeInputRecord<DrawIvyStemRecord> inputRecord,
    out indices uint3 tris[numOutputTriangles],
    out vertices VertexOutputAttributes verts[numOutputVertices])
{
    const float4x4 transform = ToFloat4x4(inputRecord.Get().transform[groupIndex]);

    Surface_Info sinfo = {
        -1,  // material_id
        -1,  // index_offset
        0,   // index_type
        -1,  // position_attribute_offset

        -1,  // texcoord0_attribute_offset
        -1,  // texcoord1_attribute_offset
        -1,  // normal_attribute_offset
        -1,  // tangent_attribute_offset

        0,   // num_indices
        0,   // num_vertices
        -1,  // weight_attribute_offset
        -1,  // joints_attribute_offset
    };

    if (IvyStemSurfaceIndex >= 0)
    {
        sinfo = g_surface_info.Load(IvyStemSurfaceIndex);
    }

    const int vertexCount   = clamp(sinfo.num_vertices, 0, numOutputVertices);
    const int triangleCount = clamp(sinfo.num_indices / 3, 0, numOutputTriangles);

    SetMeshOutputCounts(vertexCount, triangleCount);

    [[unroll]]
    for (int i = 0; i < numOutputVertexIterations; ++i)
    {
        const int vertId = threadIndex + threadGroupSize * i;

        if (vertId < vertexCount)
        {
            const float3 vertexPosition     = FetchFloat3(sinfo.position_attribute_offset, vertId);
            const float4 worldSpacePosition = mul(transform, float4(vertexPosition, 1));

            VertexOutputAttributes vertex;
            vertex.clipSpacePosition = mul(ViewProjection, worldSpacePosition);

            vertex.normal = FetchFloat3(sinfo.normal_attribute_offset, vertId);
            vertex.normal = mul((float3x3)transform, vertex.normal);

            vertex.tangent     = FetchFloat4(sinfo.tangent_attribute_offset, vertId);
            vertex.tangent.xyz = mul((float3x3)transform, vertex.tangent.xyz);

            vertex.texCoord   = FetchFloat2(sinfo.texcoord0_attribute_offset, vertId);
            vertex.materialId = sinfo.material_id;

            const float4 previousClipSpacePosition = mul(PreviousViewProjection, worldSpacePosition);
            vertex.clipSpaceMotion = (previousClipSpacePosition.xy / previousClipSpacePosition.w) - (vertex.clipSpacePosition.xy / vertex.clipSpacePosition.w);

            verts[vertId] = vertex;
        }
    }

    [[unroll]]
    for (int i = 0; i < numOutputTriangleIterations; ++i)
    {
        const int triId = threadIndex + threadGroupSize * i;

        if (triId < triangleCount)
        {
            const uint3 indices =
                (sinfo.index_type == SURFACE_INFO_INDEX_TYPE_U16) ? FetchIndicesU16(sinfo.index_offset, triId) : FetchIndicesU32(sinfo.index_offset, triId);

            tris[triId] = min(indices, vertexCount);
        }
    }
}

DeferredPixelShaderOutput PixelShader(in VertexOutputAttributes input)
{
    DeferredPixelShaderOutput output;

    Material_Info material = g_material_info.Load(input.materialId);

    output.albedo = 1.f;

    if (material.albedo_tex_id >= 0)
    {
        output.albedo =
            output.albedo * g_textures[material.albedo_tex_id].Sample(g_samplers[material.albedo_tex_sampler_id], input.texCoord);
    }

    output.normal.xyz          = input.normal;
    output.normal.a            = 1;
    output.aoMetallicRoughness = float4(material.arm_factor_x, material.arm_factor_y, material.arm_factor_z, 0);
    output.motion              = input.clipSpaceMotion;

    return output;
}