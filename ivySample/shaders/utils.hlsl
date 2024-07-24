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

#define PI 3.14159265359

// ========================
// Bit Utils

bool IsBitSet(in uint data, in int bitIndex)
{
    return data & (1u << bitIndex);
}

int BitSign(in uint data, in int bitIndex)
{
    return IsBitSet(data, bitIndex) ? 1 : -1;
}

// ========================
// Randon & Noise functions

uint Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

uint CombineSeed(uint a, uint b)
{
    return a ^ Hash(b) + 0x9e3779b9 + (a << 6) + (a >> 2);
}

uint CombineSeed(uint a, uint b, uint c)
{
    return CombineSeed(CombineSeed(a, b), c);
}

uint CombineSeed(uint a, uint b, uint c, uint d)
{
    return CombineSeed(CombineSeed(a, b), c, d);
}

uint Hash(in float seed)
{
    return Hash(asuint(seed));
}

uint Hash(in float3 vec)
{
    return CombineSeed(Hash(vec.x), Hash(vec.y), Hash(vec.z));
}

uint Hash(in float4 vec)
{
    return CombineSeed(Hash(vec.x), Hash(vec.y), Hash(vec.z), Hash(vec.w));
}

uint Hash(in float4x4 mat)
{
    return CombineSeed(Hash(mat[0]), Hash(mat[1]), Hash(mat[2]), Hash(mat[3]));
}

float Random(uint seed)
{
    return Hash(seed) / float(~0u);
}

float Random(uint a, uint b)
{
    return Random(CombineSeed(a, b));
}

float Random(uint a, uint b, uint c)
{
    return Random(CombineSeed(a, b), c);
}

float Random(uint a, uint b, uint c, uint d)
{
    return Random(CombineSeed(a, b), c, d);
}

float Random(uint a, uint b, uint c, uint d, uint e)
{
    return Random(CombineSeed(a, b), c, d, e);
}

// ========================
// Matrix Utils

template <typename T>
T IdentityMatrix()
{
    // float4x4 identity matrix should(TM) convert to identity matrix for smaller matrices
    return (T)float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
}

template <>
float3x3 IdentityMatrix<float3x3>()
{
    return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 0);
}

template <>
float4x4 IdentityMatrix<float4x4>()
{
    return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
}

float4x4 ToFloat4x4(in float3x4 mat)
{
    return float4x4(mat[0], mat[1], mat[2], float4(0, 0, 0, 1));
}

float4x4 mmul(float4x4 a)
{
    return a;
}

float4x4 mmul(float4x4 a, float4x4 b)
{
    return mul(a, b);
}

float4x4 mmul(float4x4 a, float4x4 b, float4x4 c)
{
    return mmul(mmul(a, b), c);
}

float4x4 mmul(float4x4 a, float4x4 b, float4x4 c, float4x4 d)
{
    return mmul(mmul(a, b, c), d);
}

float4x4 mmul(float4x4 a, float4x4 b, float4x4 c, float4x4 d, float4x4 e)
{
    return mmul(mmul(a, b, c, d), e);
}

float4x4 mmul(float4x4 a, float4x4 b, float4x4 c, float4x4 d, float4x4 e, float4x4 f)
{
    return mmul(mmul(a, b, c, d, e), f);
}

float4x4 mmul(float4x4 a, float4x4 b, float4x4 c, float4x4 d, float4x4 e, float4x4 f, float4x4 g)
{
    return mmul(mmul(a, b, c, d, e, f), g);
}

float4x4 RotateX(float a) {
    return float4x4(
        1, 0, 0, 0,
        0, cos(a), -sin(a), 0,
        0, sin(a), cos(a), 0,
        0, 0, 0, 1
    );
}

float4x4 RotateY(float a) {
    return float4x4(
        cos(a), 0, sin(a), 0,
        0, 1, 0, 0,
        -sin(a), 0, cos(a), 0,
        0, 0, 0, 1
    );
}

float4x4 RotateZ(float a) {
    return float4x4(
        cos(a), -sin(a), 0, 0,
        sin(a), cos(a), 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

float4x4 Rotate(float3 forward, float3 up)
{
    float4x4 rot = IdentityMatrix<float4x4>();
    rot[1].xyz   = normalize(up);
    rot[2].xyz   = normalize(cross(forward, rot[1].xyz));
    rot[0].xyz   = normalize(cross(rot[1].xyz, rot[2].xyz));
    return transpose(rot);
}

float4x4 Translate(float tx, float ty, float tz){
    return float4x4(
        1, 0, 0, tx,
        0, 1, 0, ty,
        0, 0, 1, tz,
        0, 0, 0, 1
    );
}

float4x4 Translate(float3 t)
{
    return Translate(t.x, t.y, t.z);
}

float4x4 Scale(float sx, float sy, float sz){
    return float4x4(
        sx, 0, 0, 0,
        0, sy, 0, 0,
        0, 0, sz, 0,
        0, 0, 0, 1
    );
}


