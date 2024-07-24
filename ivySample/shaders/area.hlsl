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

#include "common.hlsl"
#include "raytracing.hlsl"

struct IvyAreaSampleRecord
{
    uint     dispatchSize : SV_DispatchGrid;
    float4x4 transform;
    uint     seed;
    uint     sampleCount;
};

static const uint ivyAreaSampleThreadGroupSize = 32;
static const uint ivyAreaSampleMaxThreadGroups = 128;

uint DivideAndRoundUp(uint dividend, uint divisor)
{
    return (dividend + divisor - 1) / divisor;
}

[Shader("node")]
[NodeIsProgramEntry]
[NodeLaunch("thread")]
void IvyArea(
    ThreadNodeInputRecord<IvyAreaRecord> inputRecord,

    [MaxRecords(1)]
    [NodeId("IvyAreaSample")]
    NodeOutput<IvyAreaSampleRecord> sampleOutput
)
{
    const IvyAreaRecord record = inputRecord.Get();

    // record.transform defines a bounding box in [-1; 1]
    // Here we compute the area of the top surface of the bounding box
    const float xScale = length(mul((float3x3)record.transform, float3(1, 0, 0))) * 2;
    const float zScale = length(mul((float3x3)record.transform, float3(0, 0, 1))) * 2;

    const float sampleArea  = xScale * zScale;
    const uint  sampleCount = sampleArea * record.density;

    ThreadNodeOutputRecords<IvyAreaSampleRecord> outputRecord = sampleOutput.GetThreadNodeOutputRecords(1);

    outputRecord.Get().dispatchSize = min(DivideAndRoundUp(sampleCount, ivyAreaSampleThreadGroupSize), ivyAreaSampleMaxThreadGroups);
    outputRecord.Get().transform    = record.transform;
    outputRecord.Get().seed         = record.seed;
    outputRecord.Get().sampleCount  = sampleCount;

    outputRecord.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(ivyAreaSampleMaxThreadGroups, 1, 1)]
[NumThreads(ivyAreaSampleThreadGroupSize, 1, 1)] 
void IvyAreaSample(
    uint dtid : SV_DispatchThreadID,

    DispatchNodeInputRecord<IvyAreaSampleRecord> inputRecord,

    [MaxRecords(ivyAreaSampleThreadGroupSize)]
    [NodeId("IvyBranch")]
    NodeOutput<IvyBranchRecord> ivyBranchOutput
)
{
    const IvyAreaSampleRecord record = inputRecord.Get();

    float3 hitPosition = 0;
    float3 hitNormal   = 0;
    bool   hit         = false;

    // trace ray from top surface (at y = 1) to bottom surface (at y = -1) of bounding box defined by record.transform
    const float3 sampleDirection = mul((float3x3)record.transform, float3(0, -2, 0));

    if (dtid < record.sampleCount)
    {
        const float3 samplePositionInBoundingBox = float3(Random(record.seed, dtid, 3732) * 2.0 - 1.0,  //
                                                          1.f,
                                                          Random(record.seed, dtid, 4561) * 2.0 - 1.0);
        const float3 samplePositionWorldSpace    = mul(record.transform, float4(samplePositionInBoundingBox, 1)).xyz;

        // tMin and tMax are relative to length of direction
        hit = TraceRay(samplePositionWorldSpace, sampleDirection, 0.f, 1.f, hitPosition, hitNormal);
    }

    ThreadNodeOutputRecords<IvyBranchRecord> outputRecord = ivyBranchOutput.GetThreadNodeOutputRecords(hit);

    if (hit)
    {
        float3 forward = normalize(cross(hitNormal, sampleDirection));

        if (any(isnan(forward)))
        {
            const float3 transformForward = mul((float3x3)record.transform, float3(1, 0, 0));
            forward                       = normalize(cross(hitNormal, transformForward));
        }

        outputRecord.Get(0).transform = mmul(
            Translate(hitPosition),
            Rotate(forward, hitNormal),
            // move origin up to not place ivy inside the surface
            Translate(0, 2 * ivyStemRadius, 0)
        );
        outputRecord.Get(0).seed = CombineSeed(record.seed, dtid);
    }

    outputRecord.OutputComplete();
}