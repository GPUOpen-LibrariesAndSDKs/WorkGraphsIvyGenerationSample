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

static const uint ivyWaveSize = 32;

static const uint ivyMaxRecursion      = 12;
static const uint ivyForwardProbeCount = 8;

groupshared uint outputStemCount;
groupshared uint outputLeafCount;

[WaveSize(ivyWaveSize)]
[Shader("node")]
[NodeIsProgramEntry]
[NodeLaunch("coalescing")]
[NumThreads(ivyWaveSize * ivyThreadGroupCoalescing, 1, 1)] 
[NodeMaxRecursionDepth(ivyMaxRecursion)]
void IvyBranch(
    [MaxRecords(ivyThreadGroupCoalescing)]
    GroupNodeInputRecords<IvyBranchRecord> inputRecord,

    uint groupThreadId : SV_GroupThreadID,

    [MaxRecords(1)]
    [NodeId("DrawIvyStem")]
    NodeOutput<DrawIvyStemRecord> drawStemOutput,

    [MaxRecords(1)]
    [NodeId("DrawIvyLeaf")]
    NodeOutput<DrawIvyLeafRecord> drawLeafOutput,
    
    // one continued output; one branch output (fork)
    [MaxRecords(2 * ivyThreadGroupCoalescing)]
    [NodeId("IvyBranch")]
    NodeOutput<IvyBranchRecord> recursiveOutput
)
{
    GroupNodeOutputRecords<DrawIvyStemRecord> ivyStemOutputRecord = drawStemOutput.GetGroupNodeOutputRecords(1);
    GroupNodeOutputRecords<DrawIvyLeafRecord> ivyLeafOutputRecord = drawLeafOutput.GetGroupNodeOutputRecords(1);

    outputStemCount = 0;
    outputLeafCount = 0;

    GroupMemoryBarrierWithGroupSync();

    const bool  writingThread    = WaveIsFirstLane();
    const uint  inputRecordIndex = groupThreadId / ivyWaveSize;
    const float hitDistanceBias  = 2 * ivyStemRadius;

    float4x4 transform    = IdentityMatrix<float4x4>();
    float    stemRotation = Random('E', 'F', 'E', 'U', '!');
    bool     hasNext      = false;

    float4x4 branchTransform = IdentityMatrix<float4x4>();
    bool     hasBranch       = false;

    if (inputRecordIndex < inputRecord.Count())
    {
        const uint seed = inputRecord.Get(inputRecordIndex).seed;

        transform = inputRecord.Get(inputRecordIndex).transform;

        for (int iteration = 0; iteration < ivyThreadGroupIterations; ++iteration) 
        {
            const float3 origin  = mul(transform, float4(0, 0, 0, 1)).xyz;
            const float3 forward = normalize(mul((float3x3)transform, float3(1, 0, 0)));
            const float3 up      = normalize(mul((float3x3)transform, float3(0, 1, 0)));

            float4x4 localTransform  = mmul(
                transform,
                RotateX((WaveGetLaneIndex() / float(ivyForwardProbeCount)) * 2 * PI),
                Translate(0, ivyStemRadius, 0)
            );
            const float3 localOrigin = mul(localTransform, float4(0, 0, 0, 1)).xyz;

            float3 forwardHitPosition = localOrigin + forward * ivyStemLength;
            float3 forwardHitNormal   = float3(0, 0, 0);
            bool   forwardHit         = false;

            if (WaveGetLaneIndex() < ivyForwardProbeCount)
            {
                forwardHit = TraceRay(localOrigin, forward, 0.f, ivyStemLength, forwardHitPosition, forwardHitNormal);
            }

            const float forwardHitDistance     = distance(localOrigin, forwardHitPosition);
            const float waveForwardHitDistance = WaveActiveMin(forwardHitDistance);

            // check if any thread hit
            forwardHit = WaveActiveAnyTrue(forwardHit);

            const float2 leafOffset         = float2(Random(seed, iteration, 238), Random(seed, iteration, 928));
            const float2 leafRotationOffset = float2(Random(seed, iteration, 456) * 2.0 - 1.0, Random(seed, iteration, 567) * 2.0 - 1.0);
            const float2 leafRotation       = float2(Random(seed, iteration, 478), Random(seed, iteration, 645));

            if (forwardHit)
            {
                const bool isMinDistanceLane    = forwardHitDistance == waveForwardHitDistance;
                const uint minDistanceLaneIndex = WaveActiveMin(isMinDistanceLane ? WaveGetLaneIndex() : WaveGetLaneCount() - 1);

                const float3 waveForwardHitPosition = WaveReadLaneAt(forwardHitPosition, minDistanceLaneIndex);
                const float3 waveForwardHitNormal   = WaveReadLaneAt(forwardHitNormal, minDistanceLaneIndex);

                const float stemScale = max(waveForwardHitDistance - hitDistanceBias, 0.f) / ivyStemLength;

                // Draw stem
                if (writingThread)
                {
                    int stemOutputIndex;
                    InterlockedAdd(outputStemCount, 1, stemOutputIndex);

                    ivyStemOutputRecord.Get().transform[stemOutputIndex] = (float3x4)mmul(
                        transform,
                        RotateX(stemRotation),
                        Scale(stemScale, 1.f, 1.f)
                    );
                }

                // Draw two leafes if stem is long enough
                if (writingThread && (stemScale > 0.5))
                {
                    int leafOutputIndex;
                    InterlockedAdd(outputLeafCount, 2, leafOutputIndex);

                    ivyLeafOutputRecord.Get().transform[leafOutputIndex + 0] = (float3x4)mmul(
                        transform,
                        Translate(leafOffset.x * stemScale * ivyStemLength, 0, 0),
                        RotateY(0.5f * leafRotationOffset.x + PI / 2.f),
                        RotateZ(0.5f * leafRotation.x)
                    );
                    ivyLeafOutputRecord.Get().transform[leafOutputIndex + 1] = (float3x4)mmul(
                        transform,
                        Translate(leafOffset.x * stemScale * ivyStemLength, 0, 0),
                        RotateY(0.5f * leafRotationOffset.x - PI / 2.f),
                        RotateZ(0.5f * leafRotation.x)
                    );
                }

                float3 side = normalize(cross(forward, waveForwardHitNormal));
    
                // check if side vector would be NaN
                if (abs(dot(forward, normalize(waveForwardHitNormal))) == 1.f)
                {
                    side = cross(up, waveForwardHitNormal);
                }
    
                // compute next transform
                transform = mmul(
                    Translate(origin + forward * (waveForwardHitDistance - hitDistanceBias)),
                    Rotate(cross(waveForwardHitNormal, side), waveForwardHitNormal),
                    RotateY(Random(seed, iteration, 46578) * 2.0 - 1.0),
                    RotateZ(0.2f)
                );
            }
            else
            {
                if (writingThread)
                {
                    // Draw stem
                    int stemOutputIndex;
                    InterlockedAdd(outputStemCount, 1, stemOutputIndex);

                    ivyStemOutputRecord.Get().transform[stemOutputIndex] = (float3x4)mmul(
                        transform,
                        RotateX(stemRotation)
                    );

                    // Draw leafes
                    int leafOutputIndex;
                    InterlockedAdd(outputLeafCount, 2, leafOutputIndex);

                    ivyLeafOutputRecord.Get().transform[leafOutputIndex + 0] = (float3x4)mmul(
                        transform,
                        Translate(leafOffset.x * ivyStemLength, 0, 0),
                        RotateY(0.5f * leafRotationOffset.x + PI / 2.f),
                        RotateZ(0.5f * leafRotation.x)
                    );
                    ivyLeafOutputRecord.Get().transform[leafOutputIndex + 1] = (float3x4)mmul(
                        transform,
                        Translate(leafOffset.x * ivyStemLength, 0, 0),
                        RotateY(0.5f * leafRotationOffset.x - PI / 2.f),
                        RotateZ(0.5f * leafRotation.x)
                    );
                }

                const float3 nextOrigin = origin + forward * ivyStemLength;
    
                const uint   laneIndex       = WaveGetLaneIndex();
                const float3 randomDirection = normalize(float3(Random(seed, iteration, laneIndex, 389),  //
                                                                Random(seed, iteration, laneIndex, 829),  //
                                                                Random(seed, iteration, laneIndex, 478)) * 2.0 - 1.0);
                // lane 0 traces downwards to check current surface, all other lanes trace a random direction
                const float3 direction = writingThread ? -up : randomDirection;
                const float  tMax      = writingThread ? 2 * ivyStemRadius : 2 * ivyStemLength;

                float3 localHitPosition, localHitNormal;
                const bool localHit = TraceRay(nextOrigin, direction, 0.f, tMax, localHitPosition, localHitNormal);
    
                // Synchronize localHit across lanes
                const bool downwardHit = WaveReadLaneFirst(localHit);
                const bool anyHit      = WaveActiveAnyTrue(localHit);
            
                if (downwardHit) {
                    // Downward surface was hit; continue on current surface.

                    const float3 downwardHitPosition = WaveReadLaneFirst(localHitPosition);
                    const float3 downwardHitNormal   = WaveReadLaneFirst(localHitNormal);
    
                    transform = mmul(
                        Translate(nextOrigin),
                        Rotate(forward, up),
                        RotateY(Random(seed, iteration, 4459)),
                        RotateZ(0.1f)
                    );
                } else if (anyHit) {
                    // No downward surface was hit, but we found another surface nearby.

                    // find lane with most forward random direction
                    const float cosAngle    = dot(direction, forward);
                    const float maxCosAngle = WaveActiveMax(localHit ? cosAngle : -1.f);
    
                    const uint randomHitLaneIndex = WaveActiveMin((cosAngle == maxCosAngle)? WaveGetLaneIndex() : WaveGetLaneCount() - 1);
    
                    const float3 randomHitNormal   = WaveReadLaneAt(localHitNormal, randomHitLaneIndex);
                    const float3 randomHitPosition = WaveReadLaneAt(localHitPosition, randomHitLaneIndex) + randomHitNormal * hitDistanceBias;
    
                    const float3 nextForward = normalize(randomHitPosition - origin);
                    const float3 side        = cross(nextForward, randomHitNormal);
    
                    transform = mmul(
                        Translate(nextOrigin),
                        Rotate(nextForward, cross(side, nextForward))
                    );
                } else {
                    // No downward surface & no nearby surface. Slowly grow downward

                    // Start with random direction
                    float3 nextForward = normalize(float3(Random(seed, iteration, 387),  //
                                                          Random(seed, iteration, 158),  //
                                                          Random(seed, iteration, 520)) * 2.0 - 1.0);
                    // Bias downwards
                    nextForward.y = -4;
                    nextForward = normalize(nextForward);
                
                    float3 nextUp = normalize(cross(nextForward, forward));
    
                    // check if side vector is NaN
                    if (abs(dot(nextForward, forward)) == 1.f)
                    {
                        nextUp = normalize(cross(nextForward, up));
                    }

                    transform = mmul(
                        Translate(nextOrigin),
                        Rotate(nextForward, nextUp)
                    );
                }

                const bool branch = (Random(seed, iteration, 437858) > 0.8f) && !hasBranch;

                if (branch)
                {
                    hasBranch = true;

                    branchTransform = mmul(transform, RotateY(-0.5f));
                    transform       = mmul(transform, RotateY(0.5f));
                }
            }

            stemRotation += 1;
        }

        hasNext = (Random(seed, 3489) < 0.2f) || (GetRemainingRecursionLevels() > 6);
    }

    // recursive output
    hasNext                     = hasNext && (GetRemainingRecursionLevels() > 0);
    hasBranch                   = hasBranch && (GetRemainingRecursionLevels() > 0);
    const int outputRecordCount = int(hasNext) + int(hasBranch);

    ThreadNodeOutputRecords<IvyBranchRecord> recursiveOutputRecord = 
        recursiveOutput.GetThreadNodeOutputRecords(writingThread ? outputRecordCount : 0);

    if (writingThread && (hasNext || hasBranch))
    {
        const uint seed = inputRecord.Get(inputRecordIndex).seed;

        if (hasNext)
        {
            recursiveOutputRecord.Get(0).transform = transform;
            recursiveOutputRecord.Get(0).seed      = CombineSeed(seed, 3487, Hash(transform));
        }

        if (hasBranch)
        {
            recursiveOutputRecord.Get(hasNext).transform = branchTransform;
            recursiveOutputRecord.Get(hasNext).seed      = CombineSeed(seed, 83497, Hash(branchTransform));
        }
    }

    recursiveOutputRecord.OutputComplete();

    GroupMemoryBarrierWithGroupSync();

    // sync groupshared counters to records
    ivyStemOutputRecord.Get().stemCount = outputStemCount;
    ivyLeafOutputRecord.Get().leafCount = outputLeafCount;

    ivyStemOutputRecord.OutputComplete();
    ivyLeafOutputRecord.OutputComplete();
}