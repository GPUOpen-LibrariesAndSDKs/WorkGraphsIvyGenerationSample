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

#include "ivycommon.h"
#include "utils.hlsl"

// ==================
// Constants

static const float ivyStemLength = 0.2f;
static const float ivyStemRadius = 0.01f;

static const uint ivyThreadGroupIterations = 4;
static const uint ivyThreadGroupCoalescing = 8;

// ===================================
// Record structs for work graph nodes

struct DrawBoundingBoxRecord {
    float4x4 transform;
    float3   color;
    float3   boundingBoxMin;
    float3   boundingBoxMax;
};

// each iteration can generate one stem
static const uint maxStemsPerRecord = ivyThreadGroupIterations * ivyThreadGroupCoalescing;

struct DrawIvyStemRecord
{
    uint     stemCount : SV_DispatchGrid;
    float3x4 transform[maxStemsPerRecord];
};

// max. two leafes per stem
static const uint maxLeavesPerRecord = 2 * maxStemsPerRecord;

struct DrawIvyLeafRecord
{
    uint     leafCount : SV_DispatchGrid;
    float3x4 transform[maxLeavesPerRecord];
};

// Output struct for deferred pixel shaders
struct DeferredPixelShaderOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 aoMetallicRoughness : SV_Target2;
    float2 motion : SV_Target3;
};