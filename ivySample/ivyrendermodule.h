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

#include "render/rendermodule.h"
#include "render/shaderbuilder.h"
#include "core/contentmanager.h"
#include "core/uimanager.h"

// common files with shaders
#include "shaders/ivycommon.h"

// d3dx12 for work graphs
#include "d3dx12/d3dx12.h"

// Forward declaration of Cauldron classes
namespace cauldron
{
    class Buffer;
    class ParameterSet;
    class PipelineObject;
    class RasterView;
    class RootSignature;
    class Texture;
}  // namespace cauldron

class IvyRenderModule : public cauldron::RenderModule, public cauldron::ContentListener
{
public:
    IvyRenderModule();
    virtual ~IvyRenderModule();

    /**
     * @brief   Initialize work graphs, UI & other contexts
     */
    void Init(const json& initData) override;

    /**
     * @brief   Execute the work graph.
     */
    void Execute(double deltaTime, cauldron::CommandList* pCmdList) override;

    /**
     * @brief Called by the framework when resolution changes.
     */
    void OnResize(const cauldron::ResolutionInfo& resInfo) override;

private:
    /**
     * @brief   Create and initialize textures required for rendering and shading.
     */
    void InitTextures();
    /**
     * @brief   Create and initialize the work graph program with mesh nodes.
     */
    void InitWorkGraphProgram();

    /**
     * @brief   Renders 3D user interface for manipulating ivy generation.
     */
    void RenderUserInterface();

    /**
     * Prepare surface information for raytracing passes.
     */
    virtual void OnNewContentLoaded(cauldron::ContentBlock* pContentBlock) override;
    /**
     * @copydoc ContentListener::OnContentUnloaded()
     */
    virtual void OnContentUnloaded(cauldron::ContentBlock* pContentBlock) override;

    int32_t AddTexture(const cauldron::Material* pMaterial, const cauldron::TextureClass textureClass, int32_t& textureSamplerIndex);
    void    RemoveTexture(int32_t index);

    const cauldron::Texture*                   m_pGBufferDepthOutput               = nullptr;
    const cauldron::RasterView*                m_pGBufferDepthRasterView           = nullptr;
    const cauldron::Texture*                   m_pGBufferAlbedoOutput              = nullptr;
    const cauldron::Texture*                   m_pGBufferNormalOutput              = nullptr;
    const cauldron::Texture*                   m_pGBufferAoRoughnessMetallicOutput = nullptr;
    const cauldron::Texture*                   m_pGBufferMotionOutput              = nullptr;
    std::array<const cauldron::RasterView*, 4> m_pGBufferRasterViews;

    cauldron::RootSignature* m_pWorkGraphRootSignature       = nullptr;
    cauldron::ParameterSet*  m_pWorkGraphParameterSet        = nullptr;
    ID3D12StateObject*       m_pWorkGraphStateObject         = nullptr;
    cauldron::Buffer*        m_pWorkGraphBackingMemoryBuffer = nullptr;
    // Program description for binding the work graph
    // contains work graph identifier & backing memory
    D3D12_SET_PROGRAM_DESC m_WorkGraphProgramDesc = {};

    // Index of entry nodes
    struct WorkGraphEntryPoints
    {
        UINT IvyBranch = 0;
        UINT IvyArea   = 0;
    } m_WorkGraphEntryPoints;

    std::vector<IvyBranchRecord> m_ivyBranchRecords;
    int                          m_selectedIvyBranch = -1;
    std::vector<IvyAreaRecord>   m_ivyAreaRecords;
    int                          m_selectedIvyArea = -1;
    bool                         m_updateIvyUI     = false;

    cauldron::UISection m_UISection;

    std::mutex m_CriticalSection;

    struct RTInfoTables
    {
        struct BoundTexture
        {
            const cauldron::Texture* pTexture = nullptr;
            uint32_t                 count    = 1;
        };

        std::vector<const cauldron::Buffer*> m_VertexBuffers;
        std::vector<const cauldron::Buffer*> m_IndexBuffers;
        std::vector<BoundTexture>            m_Textures;
        std::vector<cauldron::Sampler*>      m_Samplers;

        std::vector<Material_Info>       m_cpuMaterialBuffer;
        std::vector<Instance_Info>       m_cpuInstanceBuffer;
        std::vector<Vectormath::Matrix4> m_cpuInstanceTransformBuffer;
        std::vector<Surface_Info>        m_cpuSurfaceBuffer;
        std::vector<uint32_t>            m_cpuSurfaceIDsBuffer;

        const cauldron::Buffer* m_pMaterialBuffer   = NULL;  // material_id -> Material buffer
        const cauldron::Buffer* m_pSurfaceBuffer    = NULL;  // surface_id -> Surface_Info buffer
        const cauldron::Buffer* m_pSurfaceIDsBuffer = NULL;  // flat array of uint32_t
        const cauldron::Buffer* m_pInstanceBuffer   = NULL;  // instance_id -> Instance_Info buffer
    } m_RTInfoTables;

    // Index of ivy stem surface in m_cpuSurfaceBuffer
    int m_ivyStemSurfaceIndex = -1;
    // Index of ivy leaf surface in m_cpuSurfaceBuffer
    int m_ivyLeafSurfaceIndex = -1;
};