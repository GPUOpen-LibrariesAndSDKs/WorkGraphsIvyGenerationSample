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

#include "ivyrendermodule.h"

#include "core/framework.h"
#include "core/scene.h"
#include "misc/assert.h"

#include "core/components/meshcomponent.h"

// Render components
#include "render/buffer.h"
#include "render/device.h"
#include "render/dynamicresourcepool.h"
#include "render/parameterset.h"
#include "render/pipelinedesc.h"
#include "render/pipelineobject.h"
#include "render/profiler.h"
#include "render/rasterview.h"
#include "render/rootsignature.h"
#include "render/rootsignaturedesc.h"
#include "render/texture.h"

// D3D12 Cauldron implementation
#include "render/dx12/buffer_dx12.h"
#include "render/dx12/commandlist_dx12.h"
#include "render/dx12/device_dx12.h"
#include "render/dx12/gpuresource_dx12.h"
#include "render/dx12/rootsignature_dx12.h"

// shader compiler
#include "shadercompiler.h"

// ImGuizmo
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

#include <sstream>
#include <unordered_map>

using namespace cauldron;

// Name for work graph program inside the state object
static const wchar_t* WorkGraphProgramName = L"WorkGraph";

IvyRenderModule::IvyRenderModule()
    : RenderModule(L"IvyRenderModule")
{
}

IvyRenderModule::~IvyRenderModule()
{
    // Delete work graph
    if (m_pWorkGraphStateObject)
        m_pWorkGraphStateObject->Release();
    if (m_pWorkGraphParameterSet)
        delete m_pWorkGraphParameterSet;
    if (m_pWorkGraphRootSignature)
        delete m_pWorkGraphRootSignature;
    if (m_pWorkGraphBackingMemoryBuffer)
        delete m_pWorkGraphBackingMemoryBuffer;
}

void IvyRenderModule::Init(const json& initData)
{
    InitTextures();
    InitWorkGraphProgram();

    // Use ImGui hooks to render 3D user interface
    ImGuiContextHook hook = {};
    hook.Callback         = [](ImGuiContext* ctx, ImGuiContextHook* hook) {
        IvyRenderModule* renderModule = reinterpret_cast<IvyRenderModule*>(hook->UserData);
        renderModule->RenderUserInterface();
    };
    hook.Type     = ImGuiContextHookType_EndFramePre;
    hook.UserData = this;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);

    m_ivyBranchRecords.emplace_back(IvyBranchRecord{Mat4::translation(Vec3(-15.2f, 4.5f, 0.f)), 4750});
    m_ivyBranchRecords.emplace_back(IvyBranchRecord{Mat4::translation(Vec3(0, 0.1f, 0))});

    m_ivyAreaRecords.emplace_back(IvyAreaRecord{Mat4::translation(Vec3(0, 17, 7)) * Mat4::scale(Vec3(15, 1, 4)), 4050, 0.14f});

    // Register for content change updates
    GetContentManager()->AddContentListener(this);

    SetModuleReady(true);
}

void IvyRenderModule::Execute(double deltaTime, cauldron::CommandList* pCmdList)
{
    std::lock_guard<std::mutex> pipelineLock(m_CriticalSection);

    // Update Ivy UI if needed
    if (m_updateIvyUI)
    {
        // Remove old UI section
        if (!m_UISection.SectionElements.empty())
        {
            GetUIManager()->UnRegisterUIElements(m_UISection);
        }

        if (m_selectedIvyBranch >= 0)
        {
            auto& ivyData = m_ivyBranchRecords[m_selectedIvyBranch];

            // Register new UI section
            m_UISection             = {};
            m_UISection.SectionName = std::string("IvyBranch[") + std::to_string(m_selectedIvyBranch) + "] Settings";
            m_UISection.AddIntSlider("Seed", reinterpret_cast<int*>(&ivyData.seed), 0, 10000);

            GetUIManager()->RegisterUIElements(m_UISection);
        }
        else if (m_selectedIvyArea >= 0)
        {
            auto& ivyData = m_ivyAreaRecords[m_selectedIvyArea];

            // Register new UI section
            m_UISection             = {};
            m_UISection.SectionName = std::string("IvyArea[") + std::to_string(m_selectedIvyArea) + "] Settings";
            m_UISection.AddIntSlider("Seed", reinterpret_cast<int*>(&ivyData.seed), 0, 10000);
            m_UISection.AddFloatSlider("Density", &ivyData.density, 0.f, 1.f);

            GetUIManager()->RegisterUIElements(m_UISection);
        }
    }

    // Get render resolution based on upscaler state
    const auto  upscaleState = GetFramework()->GetUpscalingState();
    const auto& resInfo      = GetFramework()->GetResolutionInfo();

    uint32_t width, height;
    if (upscaleState == UpscalerState::None || upscaleState == UpscalerState::PostUpscale)
    {
        width  = resInfo.DisplayWidth;
        height = resInfo.DisplayHeight;
    }
    else
    {
        width  = resInfo.RenderWidth;
        height = resInfo.RenderHeight;
    }

    GPUScopedProfileCapture shadingMarker(pCmdList, L"Ivy Generation");

    std::vector<Barrier> barriers;
    barriers.push_back(Barrier::Transition(m_pGBufferAlbedoOutput->GetResource(),
                                           ResourceState::NonPixelShaderResource | ResourceState::PixelShaderResource,
                                           ResourceState::RenderTargetResource));
    barriers.push_back(Barrier::Transition(m_pGBufferNormalOutput->GetResource(),
                                           ResourceState::NonPixelShaderResource | ResourceState::PixelShaderResource,
                                           ResourceState::RenderTargetResource));
    barriers.push_back(Barrier::Transition(m_pGBufferAoRoughnessMetallicOutput->GetResource(),
                                           ResourceState::NonPixelShaderResource | ResourceState::PixelShaderResource,
                                           ResourceState::RenderTargetResource));
    barriers.push_back(Barrier::Transition(m_pGBufferMotionOutput->GetResource(),
                                           ResourceState::NonPixelShaderResource | ResourceState::PixelShaderResource,
                                           ResourceState::RenderTargetResource));
    barriers.push_back(Barrier::Transition(
        m_pGBufferDepthOutput->GetResource(), ResourceState::NonPixelShaderResource | ResourceState::PixelShaderResource, ResourceState::DepthWrite));

    ResourceBarrier(pCmdList, static_cast<uint32_t>(barriers.size()), barriers.data());

    // Begin raster with render targets
    BeginRaster(pCmdList, static_cast<uint32_t>(m_pGBufferRasterViews.size()), m_pGBufferRasterViews.data(), m_pGBufferDepthRasterView, nullptr);
    SetViewportScissorRect(pCmdList, 0, 0, width, height, 0.f, 1.f);

    const auto* currentCamera = GetScene()->GetCurrentCamera();

    WorkGraphCBData workGraphData        = {};
    workGraphData.ViewProjection         = currentCamera->GetProjectionJittered() * currentCamera->GetView();
    workGraphData.PreviousViewProjection = currentCamera->GetPrevProjectionJittered() * currentCamera->GetPreviousView();
    workGraphData.InverseViewProjection  = InverseMatrix(workGraphData.ViewProjection);
    workGraphData.CameraPosition         = currentCamera->GetCameraTranslation();
    workGraphData.PreviousCameraPosition = InverseMatrix(currentCamera->GetPreviousView()).getCol3();
    workGraphData.IvyStemSurfaceIndex    = m_ivyStemSurfaceIndex;
    workGraphData.IvyLeafSurfaceIndex    = m_ivyLeafSurfaceIndex;

    BufferAddressInfo workGraphDataInfo = GetDynamicBufferPool()->AllocConstantBuffer(sizeof(WorkGraphCBData), &workGraphData);
    m_pWorkGraphParameterSet->UpdateRootConstantBuffer(&workGraphDataInfo, 0);

    m_pWorkGraphParameterSet->SetAccelerationStructure(GetScene()->GetASManager()->GetTLAS(), 0);

    // Bind all the parameters
    m_pWorkGraphParameterSet->Bind(pCmdList, nullptr);

    // Dispatch the work graph
    {
        D3D12_NODE_CPU_INPUT inputs[3];

        // IvyBranch records
        inputs[0].EntrypointIndex     = m_WorkGraphEntryPoints.IvyBranch;
        inputs[0].NumRecords          = static_cast<UINT>(m_ivyBranchRecords.size());
        inputs[0].pRecords            = m_ivyBranchRecords.data();
        inputs[0].RecordStrideInBytes = sizeof(IvyBranchRecord);

        inputs[1].EntrypointIndex     = m_WorkGraphEntryPoints.IvyArea;
        inputs[1].NumRecords          = static_cast<UINT>(m_ivyAreaRecords.size());
        inputs[1].pRecords            = m_ivyAreaRecords.data();
        inputs[1].RecordStrideInBytes = sizeof(IvyAreaRecord);

        D3D12_DISPATCH_GRAPH_DESC dispatchDesc                = {};
        dispatchDesc.Mode                                     = D3D12_DISPATCH_MODE_MULTI_NODE_CPU_INPUT;
        dispatchDesc.MultiNodeCPUInput                        = {};
        dispatchDesc.MultiNodeCPUInput.NumNodeInputs          = 2;
        dispatchDesc.MultiNodeCPUInput.pNodeInputs            = inputs;
        dispatchDesc.MultiNodeCPUInput.NodeInputStrideInBytes = sizeof(D3D12_NODE_CPU_INPUT);

        // Get ID3D12GraphicsCommandList10 from Cauldron command list
        ID3D12GraphicsCommandList10* commandList;
        CauldronThrowOnFail(pCmdList->GetImpl()->DX12CmdList()->QueryInterface(IID_PPV_ARGS(&commandList)));

        commandList->SetProgram(&m_WorkGraphProgramDesc);
        commandList->DispatchGraph(&dispatchDesc);

        // Release command list (only releases additional reference created by QueryInterface)
        commandList->Release();

        // Clear backing memory initialization flag, as the graph has run at least once now
        m_WorkGraphProgramDesc.WorkGraph.Flags &= ~D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
    }

    EndRaster(pCmdList, nullptr);

    // Transition render targets back to readable state
    for (auto& barrier : barriers)
    {
        std::swap(barrier.DestState, barrier.SourceState);
    }

    ResourceBarrier(pCmdList, static_cast<uint32_t>(barriers.size()), barriers.data());
}

void IvyRenderModule::OnResize(const cauldron::ResolutionInfo& resInfo)
{
}

void IvyRenderModule::InitTextures()
{
    m_pGBufferAlbedoOutput              = GetFramework()->GetRenderTexture(L"GBufferAlbedoRT");
    m_pGBufferNormalOutput              = GetFramework()->GetRenderTexture(L"GBufferNormalRT");
    m_pGBufferAoRoughnessMetallicOutput = GetFramework()->GetRenderTexture(L"GBufferAoRoughnessMetallicRT");
    m_pGBufferMotionOutput              = GetFramework()->GetRenderTexture(L"GBufferMotionVectorRT");
    m_pGBufferDepthOutput               = GetFramework()->GetRenderTexture(L"GBufferDepth");

    m_pGBufferRasterViews[0] = GetRasterViewAllocator()->RequestRasterView(m_pGBufferAlbedoOutput, ViewDimension::Texture2D);
    m_pGBufferRasterViews[1] = GetRasterViewAllocator()->RequestRasterView(m_pGBufferNormalOutput, ViewDimension::Texture2D);
    m_pGBufferRasterViews[2] = GetRasterViewAllocator()->RequestRasterView(m_pGBufferAoRoughnessMetallicOutput, ViewDimension::Texture2D);
    m_pGBufferRasterViews[3] = GetRasterViewAllocator()->RequestRasterView(m_pGBufferMotionOutput, ViewDimension::Texture2D);

    m_pGBufferDepthRasterView = GetRasterViewAllocator()->RequestRasterView(m_pGBufferDepthOutput, ViewDimension::Texture2D);
}

void IvyRenderModule::InitWorkGraphProgram()
{
    // Create root signature for work graph
    RootSignatureDesc workGraphRootSigDesc;
    workGraphRootSigDesc.AddConstantBufferView(0, ShaderBindStage::Compute, 1);
    workGraphRootSigDesc.AddRTAccelerationStructureSet(0, ShaderBindStage::Compute, 1);

    workGraphRootSigDesc.AddBufferSRVSet(RAYTRACING_INFO_BEGIN_SLOT + 0, ShaderBindStage::Compute, 1);
    workGraphRootSigDesc.AddBufferSRVSet(RAYTRACING_INFO_BEGIN_SLOT + 1, ShaderBindStage::Compute, 1);
    workGraphRootSigDesc.AddBufferSRVSet(RAYTRACING_INFO_BEGIN_SLOT + 2, ShaderBindStage::Compute, 1);
    workGraphRootSigDesc.AddBufferSRVSet(RAYTRACING_INFO_BEGIN_SLOT + 3, ShaderBindStage::Compute, 1);

    workGraphRootSigDesc.AddTextureSRVSet(TEXTURE_BEGIN_SLOT, ShaderBindStage::Compute, MAX_TEXTURES_COUNT);

    workGraphRootSigDesc.AddBufferSRVSet(INDEX_BUFFER_BEGIN_SLOT, ShaderBindStage::Compute, MAX_BUFFER_COUNT);
    workGraphRootSigDesc.AddBufferSRVSet(VERTEX_BUFFER_BEGIN_SLOT, ShaderBindStage::Compute, MAX_BUFFER_COUNT);

    workGraphRootSigDesc.AddSamplerSet(SAMPLER_BEGIN_SLOT, ShaderBindStage::Compute, MAX_SAMPLERS_COUNT);

    workGraphRootSigDesc.m_PipelineType = PipelineType::Graphics;

    m_pWorkGraphRootSignature = RootSignature::CreateRootSignature(L"MeshNodeSample_WorkGraphRootSignature", workGraphRootSigDesc);

    // Create parameter set for root signature
    m_pWorkGraphParameterSet = ParameterSet::CreateParameterSet(m_pWorkGraphRootSignature);
    m_pWorkGraphParameterSet->SetRootConstantBufferResource(GetDynamicBufferPool()->GetResource(), sizeof(WorkGraphCBData), 0);

    // Get D3D12 device
    // CreateStateObject is only available on ID3D12Device9
    ID3D12Device9* d3dDevice = nullptr;
    CauldronThrowOnFail(GetDevice()->GetImpl()->DX12Device()->QueryInterface(IID_PPV_ARGS(&d3dDevice)));

    // Check if mesh nodes are supported
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS21 options = {};
        CauldronThrowOnFail(d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &options, sizeof(options)));

        // check if work graphs tier 1.1 (mesh nodes) is supported
        if (options.WorkGraphsTier < D3D12_WORK_GRAPHS_TIER_1_1)
        {
            CauldronCritical(L"Work graphs tier 1.1 (mesh nodes) are not supported on the current device.");
        }
    }

    // Create work graph
    CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_EXECUTABLE);

    // configure draw nodes to use graphics root signature
    auto configSubobject = stateObjectDesc.CreateSubobject<CD3DX12_STATE_OBJECT_CONFIG_SUBOBJECT>();
    configSubobject->SetFlags(D3D12_STATE_OBJECT_FLAG_WORK_GRAPHS_USE_GRAPHICS_STATE_FOR_GLOBAL_ROOT_SIGNATURE);

    // set root signature for work graph
    auto rootSignatureSubobject = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    rootSignatureSubobject->SetRootSignature(m_pWorkGraphRootSignature->GetImpl()->DX12RootSignature());

    auto workgraphSubobject = stateObjectDesc.CreateSubobject<CD3DX12_WORK_GRAPH_SUBOBJECT>();
    workgraphSubobject->IncludeAllAvailableNodes();
    workgraphSubobject->SetProgramName(WorkGraphProgramName);

    // add DXIL shader libraries
    ShaderCompiler shaderCompiler;

    // list of compiled shaders to be released once the work graph is created
    std::vector<IDxcBlob*> compiledShaders;

    // Helper function for adding a shader library to the work graph state object
    const auto AddShaderLibrary = [&](const wchar_t* shaderFileName) {
        // compile shader as library
        auto* blob           = shaderCompiler.CompileShader(shaderFileName, L"lib_6_9", nullptr);
        auto  shaderBytecode = CD3DX12_SHADER_BYTECODE(blob->GetBufferPointer(), blob->GetBufferSize());

        // add blob to state object
        auto librarySubobject = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        librarySubobject->SetDXILLibrary(&shaderBytecode);

        // add shader blob to be released later
        compiledShaders.push_back(blob);
    };

    // Helper function for adding a pixel shader to the work graph state object
    // Pixel shaders need to be compiled with "ps" target and as such the DXIL library object needs to specify a name
    // for the pixel shader (exportName) with which the generic program can reference the pixel shader
    const auto AddPixelShader = [&](const wchar_t* shaderFileName, const wchar_t* entryPoint, const wchar_t* exportName) {
        // compile shader as pixel shader
        auto* blob           = shaderCompiler.CompileShader(shaderFileName, L"ps_6_9", entryPoint);
        auto  shaderBytecode = CD3DX12_SHADER_BYTECODE(blob->GetBufferPointer(), blob->GetBufferSize());

        // add blob to state object
        auto librarySubobject = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        librarySubobject->SetDXILLibrary(&shaderBytecode);

        // define pixel shader export
        librarySubobject->DefineExport(exportName, L"*");

        // add shader blob to be released later
        compiledShaders.push_back(blob);
    };

    // ===================================================================
    // State object for graphics PSO state description in generic programs

    // Rasterizer state configuration without culling
    auto rasterizerNoCullingSubobject = stateObjectDesc.CreateSubobject<CD3DX12_RASTERIZER_SUBOBJECT>();
    rasterizerNoCullingSubobject->SetFrontCounterClockwise(true);
    rasterizerNoCullingSubobject->SetFillMode(D3D12_FILL_MODE_SOLID);
    rasterizerNoCullingSubobject->SetCullMode(D3D12_CULL_MODE_NONE);

    // Rasterizer state configuration with backface culling
    auto rasterizerBackfaceCullingSubobject = stateObjectDesc.CreateSubobject<CD3DX12_RASTERIZER_SUBOBJECT>();
    rasterizerBackfaceCullingSubobject->SetFrontCounterClockwise(true);
    rasterizerBackfaceCullingSubobject->SetFillMode(D3D12_FILL_MODE_SOLID);
    rasterizerBackfaceCullingSubobject->SetCullMode(D3D12_CULL_MODE_BACK);

    // Primitive topology configuration
    auto primitiveTopologySubobject = stateObjectDesc.CreateSubobject<CD3DX12_PRIMITIVE_TOPOLOGY_SUBOBJECT>();
    primitiveTopologySubobject->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

    // Depth stencil format configuration
    auto depthStencilFormatSubobject = stateObjectDesc.CreateSubobject<CD3DX12_DEPTH_STENCIL_FORMAT_SUBOBJECT>();
    depthStencilFormatSubobject->SetDepthStencilFormat(GetDXGIFormat(m_pGBufferDepthOutput->GetFormat()));

    //  Render target format configuration
    auto renderTargetFormatSubobject = stateObjectDesc.CreateSubobject<CD3DX12_RENDER_TARGET_FORMATS_SUBOBJECT>();
    renderTargetFormatSubobject->SetNumRenderTargets(4);
    renderTargetFormatSubobject->SetRenderTargetFormat(0, GetDXGIFormat(m_pGBufferAlbedoOutput->GetFormat()));
    renderTargetFormatSubobject->SetRenderTargetFormat(1, GetDXGIFormat(m_pGBufferNormalOutput->GetFormat()));
    renderTargetFormatSubobject->SetRenderTargetFormat(2, GetDXGIFormat(m_pGBufferAoRoughnessMetallicOutput->GetFormat()));
    renderTargetFormatSubobject->SetRenderTargetFormat(3, GetDXGIFormat(m_pGBufferMotionOutput->GetFormat()));

    // =============================
    // Generic programs (mesh nodes)

    // Helper function to add a mesh node generic program subobject
    const auto AddMeshNode = [&](const wchar_t* meshShaderExportName, const wchar_t* pixelShaderExportName, bool backfaceCulling) {
        auto genericProgramSubobject = stateObjectDesc.CreateSubobject<CD3DX12_GENERIC_PROGRAM_SUBOBJECT>();
        // add mesh shader
        genericProgramSubobject->AddExport(meshShaderExportName);
        // add pixel shader
        genericProgramSubobject->AddExport(pixelShaderExportName);

        // add graphics state subobjects
        if (backfaceCulling)
        {
            genericProgramSubobject->AddSubobject(*rasterizerBackfaceCullingSubobject);
        }
        else
        {
            genericProgramSubobject->AddSubobject(*rasterizerNoCullingSubobject);
        }
        genericProgramSubobject->AddSubobject(*primitiveTopologySubobject);
        genericProgramSubobject->AddSubobject(*depthStencilFormatSubobject);
        genericProgramSubobject->AddSubobject(*renderTargetFormatSubobject);
    };

    // ===================================
    // Add shader libraries and mesh nodes

    // Shader libraries for ivy generation
    AddShaderLibrary(L"area.hlsl");
    AddShaderLibrary(L"ivy.hlsl");

    AddShaderLibrary(L"ivystemrenderer.hlsl");
    AddPixelShader(L"ivystemrenderer.hlsl", L"PixelShader", L"IvyStemPixelShader");
    AddMeshNode(L"IvyStemMeshShader", L"IvyStemPixelShader", true);

    AddShaderLibrary(L"ivyleafrenderer.hlsl");
    AddPixelShader(L"ivyleafrenderer.hlsl", L"PixelShader", L"IvyLeafPixelShader");
    AddMeshNode(L"IvyLeafMeshShader", L"IvyLeafPixelShader", true);

    // Create work graph state object
    CauldronThrowOnFail(d3dDevice->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&m_pWorkGraphStateObject)));

    // release all compiled shaders
    for (auto* shader : compiledShaders)
    {
        if (shader)
        {
            shader->Release();
        }
    }

    // Get work graph properties
    ID3D12StateObjectProperties1* stateObjectProperties;
    ID3D12WorkGraphProperties1*   workGraphProperties;

    CauldronThrowOnFail(m_pWorkGraphStateObject->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
    CauldronThrowOnFail(m_pWorkGraphStateObject->QueryInterface(IID_PPV_ARGS(&workGraphProperties)));

    // Get the index of our work graph inside the state object (state object can contain multiple work graphs)
    const auto workGraphIndex = workGraphProperties->GetWorkGraphIndex(WorkGraphProgramName);

    // Set the input record limit. This is required for work graphs with mesh nodes.
    workGraphProperties->SetMaximumInputRecords(workGraphIndex, static_cast<UINT>(m_ivyBranchRecords.size() + m_ivyAreaRecords.size()), 2);

    // Create backing memory buffer
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS memoryRequirements = {};
    workGraphProperties->GetWorkGraphMemoryRequirements(workGraphIndex, &memoryRequirements);
    if (memoryRequirements.MaxSizeInBytes > 0)
    {
        BufferDesc bufferDesc = BufferDesc::Data(L"MeshNodeSample_WorkGraphBackingMemory",
                                                 static_cast<uint32_t>(memoryRequirements.MaxSizeInBytes),
                                                 1,
                                                 D3D12_WORK_GRAPHS_BACKING_MEMORY_ALIGNMENT_IN_BYTES,
                                                 ResourceFlags::AllowUnorderedAccess);

        m_pWorkGraphBackingMemoryBuffer = Buffer::CreateBufferResource(&bufferDesc, ResourceState::UnorderedAccess);
    }

    // Prepare work graph desc
    m_WorkGraphProgramDesc.Type                        = D3D12_PROGRAM_TYPE_WORK_GRAPH;
    m_WorkGraphProgramDesc.WorkGraph.ProgramIdentifier = stateObjectProperties->GetProgramIdentifier(WorkGraphProgramName);
    // Set flag to initialize backing memory.
    // We'll clear this flag once we've run the work graph for the first time.
    m_WorkGraphProgramDesc.WorkGraph.Flags = D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
    // Set backing memory
    if (m_pWorkGraphBackingMemoryBuffer)
    {
        const auto addressInfo                                      = m_pWorkGraphBackingMemoryBuffer->GetAddressInfo();
        m_WorkGraphProgramDesc.WorkGraph.BackingMemory.StartAddress = addressInfo.GetImpl()->GPUBufferView;
        m_WorkGraphProgramDesc.WorkGraph.BackingMemory.SizeInBytes  = addressInfo.GetImpl()->SizeInBytes;
    }

    // Query entry point indices
    m_WorkGraphEntryPoints.IvyBranch = workGraphProperties->GetEntrypointIndex(workGraphIndex, {L"IvyBranch", 0});
    m_WorkGraphEntryPoints.IvyArea   = workGraphProperties->GetEntrypointIndex(workGraphIndex, {L"IvyArea", 0});

    // Release state object properties
    stateObjectProperties->Release();
    workGraphProperties->Release();

    // Release ID3D12Device9 (only releases additional reference created by QueryInterface)
    d3dDevice->Release();
}

void IvyRenderModule::RenderUserInterface()
{
    const auto* currentCamera = GetScene()->GetCurrentCamera();
    const auto& resInfo       = GetFramework()->GetResolutionInfo();

    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0, 0, static_cast<float>(resInfo.DisplayWidth), static_cast<float>(resInfo.DisplayHeight));

    for (int i = 0; i < m_ivyBranchRecords.size(); ++i)
    {
        auto& ivyData = m_ivyBranchRecords[i];

        if (i == m_selectedIvyBranch)
        {
            ImGuizmo::Manipulate(reinterpret_cast<const float*>(&currentCamera->GetView()),
                                 reinterpret_cast<const float*>(&currentCamera->GetProjection()),
                                 ImGuizmo::OPERATION::TRANSLATE | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z,
                                 ImGuizmo::WORLD,
                                 reinterpret_cast<float*>(&ivyData.transform),
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr);
        }
        else
        {
            if (ImGuizmo::Select(reinterpret_cast<const float*>(&currentCamera->GetView()),
                                 reinterpret_cast<const float*>(&currentCamera->GetProjection()),
                                 reinterpret_cast<float*>(&ivyData.transform),
                                 nullptr))
            {
                m_selectedIvyBranch = i;
                // deselect ivy area
                m_selectedIvyArea = -1;
                m_updateIvyUI     = true;
            }
        }
    }

    // Bounds for ivy area box
    static float bounds[6]{-1.f, -1.f, -1.f, 1.f, 1.f, 1.f};

    for (int i = 0; i < m_ivyAreaRecords.size(); ++i)
    {
        auto& ivyData = m_ivyAreaRecords[i];

        if (i == m_selectedIvyArea)
        {
            ImGuizmo::Manipulate(reinterpret_cast<const float*>(&currentCamera->GetView()),
                                 reinterpret_cast<const float*>(&currentCamera->GetProjection()),
                                 ImGuizmo::OPERATION::TRANSLATE | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z,
                                 ImGuizmo::WORLD,
                                 reinterpret_cast<float*>(&ivyData.transform),
                                 nullptr,
                                 nullptr,
                                 bounds,
                                 nullptr);
        }
        else
        {
            if (ImGuizmo::Select(reinterpret_cast<const float*>(&currentCamera->GetView()),
                                 reinterpret_cast<const float*>(&currentCamera->GetProjection()),
                                 reinterpret_cast<float*>(&ivyData.transform),
                                 nullptr))
            {
                m_selectedIvyArea = i;
                // deselect ivy branch
                m_selectedIvyBranch = -1;
                m_updateIvyUI       = true;
            }
        }
    }
}

void IvyRenderModule::OnNewContentLoaded(ContentBlock* pContentBlock)
{
    std::lock_guard<std::mutex> pipelineLock(m_CriticalSection);
    // Material

    const size_t materialIdOffset = m_RTInfoTables.m_cpuMaterialBuffer.size();

    for (auto* pMat : pContentBlock->Materials)
    {
        Material_Info materialInfo;

        materialInfo.albedo_factor_x = pMat->GetAlbedoColor().getX();
        materialInfo.albedo_factor_y = pMat->GetAlbedoColor().getY();
        materialInfo.albedo_factor_z = pMat->GetAlbedoColor().getZ();
        materialInfo.albedo_factor_w = pMat->GetAlbedoColor().getW();

        materialInfo.emission_factor_x = pMat->GetEmissiveColor().getX();
        materialInfo.emission_factor_y = pMat->GetEmissiveColor().getY();
        materialInfo.emission_factor_z = pMat->GetEmissiveColor().getZ();

        materialInfo.arm_factor_x = 1.0f;
        materialInfo.arm_factor_y = pMat->GetPBRInfo().getY();
        materialInfo.arm_factor_z = pMat->GetPBRInfo().getX();

        materialInfo.is_opaque    = pMat->GetBlendMode() == MaterialBlend::Opaque;
        materialInfo.alpha_cutoff = pMat->GetAlphaCutOff();

        int32_t samplerIndex;
        if (pMat->HasPBRInfo())
        {
            materialInfo.albedo_tex_id         = AddTexture(pMat, TextureClass::Albedo, samplerIndex);
            materialInfo.albedo_tex_sampler_id = samplerIndex;

            if (pMat->HasPBRMetalRough())
            {
                materialInfo.arm_tex_id         = AddTexture(pMat, TextureClass::MetalRough, samplerIndex);
                materialInfo.arm_tex_sampler_id = samplerIndex;
            }
            else if (pMat->HasPBRSpecGloss())
            {
                materialInfo.arm_tex_id         = AddTexture(pMat, TextureClass::SpecGloss, samplerIndex);
                materialInfo.arm_tex_sampler_id = samplerIndex;
            }
        }

        materialInfo.normal_tex_id           = AddTexture(pMat, TextureClass::Normal, samplerIndex);
        materialInfo.normal_tex_sampler_id   = samplerIndex;
        materialInfo.emission_tex_id         = AddTexture(pMat, TextureClass::Emissive, samplerIndex);
        materialInfo.emission_tex_sampler_id = samplerIndex;

        m_RTInfoTables.m_cpuMaterialBuffer.push_back(materialInfo);
    }

    MeshComponentMgr* pMeshComponentManager = MeshComponentMgr::Get();

    std::unordered_map<uint32_t, const Mesh*> meshIdxToMesh;

    uint32_t nodeID = 0, surfaceID = 0;
    for (auto* pEntityData : pContentBlock->EntityDataBlocks)
    {
        for (auto* pComponent : pEntityData->Components)
        {
            if (pComponent->GetManager() == pMeshComponentManager)
            {
                const Mesh* pMesh = reinterpret_cast<MeshComponent*>(pComponent)->GetData().pMesh;

                if (meshIdxToMesh.find(pMesh->GetMeshIndex()) != meshIdxToMesh.end())
                {
                    continue;
                }

                meshIdxToMesh.emplace(pMesh->GetMeshIndex(), pMesh);

                Instance_Info instance_info{};
                instance_info.surface_id_table_offset = (uint32_t)m_RTInfoTables.m_cpuSurfaceIDsBuffer.size();

                const size_t numSurfaces       = pMesh->GetNumSurfaces();
                size_t       numOpaqueSurfaces = 0;

                struct MeshData
                {
                    BLAS*                 m_pBlas;
                    uint32_t              m_Index;
                    std::wstring          m_Name;
                    std::vector<Surface*> m_Surfaces;
                };

                const MeshData* pMeshData = reinterpret_cast<const MeshData*>(pMesh);

                if (pMeshData->m_Name == L"..\\media\\Ivy\\Stem")
                {
                    m_ivyStemSurfaceIndex = static_cast<int>(m_RTInfoTables.m_cpuSurfaceBuffer.size());
                }

                if (pMeshData->m_Name == L"..\\media\\Ivy\\Leaf")
                {
                    m_ivyLeafSurfaceIndex = static_cast<int>(m_RTInfoTables.m_cpuSurfaceBuffer.size());
                }

                for (uint32_t i = 0; i < numSurfaces; ++i)
                {
                    const Surface*  pSurface  = pMesh->GetSurface(i);
                    const Material* pMaterial = pSurface->GetMaterial();

                    m_RTInfoTables.m_cpuSurfaceIDsBuffer.push_back(static_cast<uint32_t>(m_RTInfoTables.m_cpuSurfaceBuffer.size()));

                    Surface_Info surface_info{};
                    memset(&surface_info, -1, sizeof(surface_info));
                    surface_info.num_indices  = pSurface->GetIndexBuffer().Count;
                    surface_info.num_vertices = pSurface->GetVertexBuffer(VertexAttributeType::Position).Count;

                    int foundIndex = -1;
                    for (size_t i = 0; i < m_RTInfoTables.m_IndexBuffers.size(); i++)
                    {
                        if (m_RTInfoTables.m_IndexBuffers[i] == pSurface->GetIndexBuffer().pBuffer)
                        {
                            foundIndex = (int)i;
                            break;
                        }
                    }

                    surface_info.index_offset = foundIndex >= 0 ? foundIndex : (int)m_RTInfoTables.m_IndexBuffers.size();
                    if (foundIndex < 0)
                        m_RTInfoTables.m_IndexBuffers.push_back(pSurface->GetIndexBuffer().pBuffer);

                    switch (pSurface->GetIndexBuffer().IndexFormat)
                    {
                    case ResourceFormat::R16_UINT:
                        surface_info.index_type = SURFACE_INFO_INDEX_TYPE_U16;
                        break;
                    case ResourceFormat::R32_UINT:
                        surface_info.index_type = SURFACE_INFO_INDEX_TYPE_U32;
                        break;
                    default:
                        CauldronError(L"Unsupported resource format for ray tracing indices");
                    }

                    uint32_t usedAttributes = VertexAttributeFlag_Position | VertexAttributeFlag_Normal | VertexAttributeFlag_Tangent |
                                              VertexAttributeFlag_Texcoord0 | VertexAttributeFlag_Texcoord1;

                    const uint32_t surfaceAttributes = pSurface->GetVertexAttributes();
                    usedAttributes                   = usedAttributes & surfaceAttributes;

                    for (uint32_t attribute = 0; attribute < static_cast<uint32_t>(VertexAttributeType::Count); ++attribute)
                    {
                        // Check if the attribute is present
                        if (usedAttributes & (0x1 << attribute))
                        {
                            int foundIndex = -1;
                            for (size_t i = 0; i < m_RTInfoTables.m_VertexBuffers.size(); i++)
                            {
                                if (m_RTInfoTables.m_VertexBuffers[i] == pSurface->GetVertexBuffer(static_cast<VertexAttributeType>(attribute)).pBuffer)
                                {
                                    foundIndex = (int)i;
                                    break;
                                }
                            }
                            if (foundIndex < 0)
                                m_RTInfoTables.m_VertexBuffers.push_back(pSurface->GetVertexBuffer(static_cast<VertexAttributeType>(attribute)).pBuffer);
                            switch (static_cast<VertexAttributeType>(attribute))
                            {
                            case cauldron::VertexAttributeType::Position:
                                surface_info.position_attribute_offset = foundIndex >= 0 ? foundIndex : (int)m_RTInfoTables.m_VertexBuffers.size() - 1;
                                break;
                            case cauldron::VertexAttributeType::Normal:
                                surface_info.normal_attribute_offset = foundIndex >= 0 ? foundIndex : (int)m_RTInfoTables.m_VertexBuffers.size() - 1;
                                break;
                            case cauldron::VertexAttributeType::Tangent:
                                surface_info.tangent_attribute_offset = foundIndex >= 0 ? foundIndex : (int)m_RTInfoTables.m_VertexBuffers.size() - 1;
                                break;
                            case cauldron::VertexAttributeType::Texcoord0:
                                surface_info.texcoord0_attribute_offset = foundIndex >= 0 ? foundIndex : (int)m_RTInfoTables.m_VertexBuffers.size() - 1;
                                break;
                            case cauldron::VertexAttributeType::Texcoord1:
                                surface_info.texcoord1_attribute_offset = foundIndex >= 0 ? foundIndex : (int)m_RTInfoTables.m_VertexBuffers.size() - 1;
                                break;
                            default:
                                break;
                            }
                        }
                    }

                    for (size_t i = 0; i < pContentBlock->Materials.size(); i++)
                    {
                        if (pContentBlock->Materials[i] == pMaterial)
                        {
                            surface_info.material_id = (uint32_t)(i + materialIdOffset);
                            break;
                        }
                    }
                    m_RTInfoTables.m_cpuSurfaceBuffer.push_back(surface_info);

                    if (!pSurface->HasTranslucency())
                        numOpaqueSurfaces++;
                }

                instance_info.num_surfaces        = (uint32_t)(numOpaqueSurfaces);
                instance_info.num_opaque_surfaces = (uint32_t)(numSurfaces);
                instance_info.node_id             = pMesh->GetMeshIndex();

                if (m_RTInfoTables.m_cpuInstanceBuffer.size() <= pMesh->GetMeshIndex())
                {
                    m_RTInfoTables.m_cpuInstanceBuffer.resize(pMesh->GetMeshIndex() + 1);
                }

                m_RTInfoTables.m_cpuInstanceBuffer[pMesh->GetMeshIndex()] = instance_info;
            }
        }
    }

    if (m_RTInfoTables.m_cpuSurfaceBuffer.size() > 0)
    {
        // Upload
        BufferDesc bufferMaterial = BufferDesc::Data(
            L"HSR_MaterialBuffer", uint32_t(m_RTInfoTables.m_cpuMaterialBuffer.size() * sizeof(Material_Info)), sizeof(Material_Info), 0, ResourceFlags::None);
        m_RTInfoTables.m_pMaterialBuffer = GetDynamicResourcePool()->CreateBuffer(&bufferMaterial, ResourceState::CopyDest);
        const_cast<Buffer*>(m_RTInfoTables.m_pMaterialBuffer)
            ->CopyData(m_RTInfoTables.m_cpuMaterialBuffer.data(), m_RTInfoTables.m_cpuMaterialBuffer.size() * sizeof(Material_Info));

        BufferDesc bufferInstance = BufferDesc::Data(
            L"HSR_InstanceBuffer", uint32_t(m_RTInfoTables.m_cpuInstanceBuffer.size() * sizeof(Instance_Info)), sizeof(Instance_Info), 0, ResourceFlags::None);
        m_RTInfoTables.m_pInstanceBuffer = GetDynamicResourcePool()->CreateBuffer(&bufferInstance, ResourceState::CopyDest);
        const_cast<Buffer*>(m_RTInfoTables.m_pInstanceBuffer)
            ->CopyData(m_RTInfoTables.m_cpuInstanceBuffer.data(), m_RTInfoTables.m_cpuInstanceBuffer.size() * sizeof(Instance_Info));

        BufferDesc bufferSurfaceID = BufferDesc::Data(
            L"HSR_SurfaceIDBuffer", uint32_t(m_RTInfoTables.m_cpuSurfaceIDsBuffer.size() * sizeof(uint32_t)), sizeof(uint32_t), 0, ResourceFlags::None);
        m_RTInfoTables.m_pSurfaceIDsBuffer = GetDynamicResourcePool()->CreateBuffer(&bufferSurfaceID, ResourceState::CopyDest);
        const_cast<Buffer*>(m_RTInfoTables.m_pSurfaceIDsBuffer)
            ->CopyData(m_RTInfoTables.m_cpuSurfaceIDsBuffer.data(), m_RTInfoTables.m_cpuSurfaceIDsBuffer.size() * sizeof(uint32_t));

        BufferDesc bufferSurface = BufferDesc::Data(
            L"HSR_SurfaceBuffer", uint32_t(m_RTInfoTables.m_cpuSurfaceBuffer.size() * sizeof(Surface_Info)), sizeof(Surface_Info), 0, ResourceFlags::None);
        m_RTInfoTables.m_pSurfaceBuffer = GetDynamicResourcePool()->CreateBuffer(&bufferSurface, ResourceState::CopyDest);
        const_cast<Buffer*>(m_RTInfoTables.m_pSurfaceBuffer)
            ->CopyData(m_RTInfoTables.m_cpuSurfaceBuffer.data(), m_RTInfoTables.m_cpuSurfaceBuffer.size() * sizeof(Surface_Info));

        m_pWorkGraphParameterSet->SetBufferSRV(m_RTInfoTables.m_pMaterialBuffer, RAYTRACING_INFO_BEGIN_SLOT);
        m_pWorkGraphParameterSet->SetBufferSRV(m_RTInfoTables.m_pInstanceBuffer, RAYTRACING_INFO_BEGIN_SLOT + 1);
        m_pWorkGraphParameterSet->SetBufferSRV(m_RTInfoTables.m_pSurfaceIDsBuffer, RAYTRACING_INFO_BEGIN_SLOT + 2);
        m_pWorkGraphParameterSet->SetBufferSRV(m_RTInfoTables.m_pSurfaceBuffer, RAYTRACING_INFO_BEGIN_SLOT + 3);
    }

    {
        // Update the parameter set with loaded texture entries
        CauldronAssert(ASSERT_CRITICAL, m_RTInfoTables.m_Textures.size() <= MAX_TEXTURES_COUNT, L"Too many textures.");
        for (uint32_t i = 0; i < m_RTInfoTables.m_Textures.size(); ++i)
        {
            m_pWorkGraphParameterSet->SetTextureSRV(m_RTInfoTables.m_Textures[i].pTexture, ViewDimension::Texture2D, i + TEXTURE_BEGIN_SLOT);
        }

        // Update sampler bindings as well
        CauldronAssert(ASSERT_CRITICAL, m_RTInfoTables.m_Samplers.size() <= MAX_SAMPLERS_COUNT, L"Too many samplers.");
        for (uint32_t i = 0; i < m_RTInfoTables.m_Samplers.size(); ++i)
        {
            m_pWorkGraphParameterSet->SetSampler(m_RTInfoTables.m_Samplers[i], i + SAMPLER_BEGIN_SLOT);
        }

        CauldronAssert(ASSERT_CRITICAL, m_RTInfoTables.m_IndexBuffers.size() <= MAX_BUFFER_COUNT, L"Too many index buffers.");
        for (uint32_t i = 0; i < m_RTInfoTables.m_IndexBuffers.size(); ++i)
        {
            m_pWorkGraphParameterSet->SetBufferSRV(m_RTInfoTables.m_IndexBuffers[i], i + INDEX_BUFFER_BEGIN_SLOT);
        }

        CauldronAssert(ASSERT_CRITICAL, m_RTInfoTables.m_VertexBuffers.size() <= MAX_BUFFER_COUNT, L"Too many vertex buffers.");
        for (uint32_t i = 0; i < m_RTInfoTables.m_VertexBuffers.size(); ++i)
        {
            m_pWorkGraphParameterSet->SetBufferSRV(m_RTInfoTables.m_VertexBuffers[i], i + VERTEX_BUFFER_BEGIN_SLOT);
        }
    }
}

void IvyRenderModule::OnContentUnloaded(ContentBlock* pContentBlock)
{
    for (auto materialInfo : m_RTInfoTables.m_cpuMaterialBuffer)
    {
        if (materialInfo.albedo_tex_id > 0)
            RemoveTexture(materialInfo.albedo_tex_id);
        if (materialInfo.arm_tex_id > 0)
            RemoveTexture(materialInfo.arm_tex_id);
        if (materialInfo.emission_tex_id > 0)
            RemoveTexture(materialInfo.emission_tex_id);
        if (materialInfo.normal_tex_id > 0)
            RemoveTexture(materialInfo.normal_tex_id);
    }
}

// Add texture index info and return the index to the texture in the texture array
int32_t IvyRenderModule::AddTexture(const Material* pMaterial, const TextureClass textureClass, int32_t& textureSamplerIndex)
{
    const cauldron::TextureInfo* pTextureInfo = pMaterial->GetTextureInfo(textureClass);
    if (pTextureInfo != nullptr)
    {
        // Check if the texture's sampler is already one we have, and if not add it
        for (textureSamplerIndex = 0; textureSamplerIndex < m_RTInfoTables.m_Samplers.size(); ++textureSamplerIndex)
        {
            if (m_RTInfoTables.m_Samplers[textureSamplerIndex]->GetDesc() == pTextureInfo->TexSamplerDesc)
                break;  // found
        }

        // If we didn't find the sampler, add it
        if (textureSamplerIndex == m_RTInfoTables.m_Samplers.size())
        {
            Sampler* pSampler = Sampler::CreateSampler(L"HSRSampler", pTextureInfo->TexSamplerDesc);
            CauldronAssert(ASSERT_WARNING, pSampler, L"Could not create sampler for loaded content %s", pTextureInfo->pTexture->GetDesc().Name.c_str());
            m_RTInfoTables.m_Samplers.push_back(pSampler);
        }

        // Find a slot for the texture
        int32_t firstFreeIndex = -1;
        for (int32_t i = 0; i < m_RTInfoTables.m_Textures.size(); ++i)
        {
            RTInfoTables::BoundTexture& boundTexture = m_RTInfoTables.m_Textures[i];

            // If this texture is already mapped, bump it's reference count
            if (pTextureInfo->pTexture == boundTexture.pTexture)
            {
                boundTexture.count += 1;
                return i;
            }

            // Try to re-use an existing entry that was released
            else if (firstFreeIndex < 0 && boundTexture.count == 0)
            {
                firstFreeIndex = i;
            }
        }

        // Texture wasn't found
        RTInfoTables::BoundTexture b = {pTextureInfo->pTexture, 1};
        if (firstFreeIndex < 0)
        {
            m_RTInfoTables.m_Textures.push_back(b);
            return static_cast<int32_t>(m_RTInfoTables.m_Textures.size()) - 1;
        }
        else
        {
            m_RTInfoTables.m_Textures[firstFreeIndex] = b;
            return firstFreeIndex;
        }
    }
    return -1;
}

void IvyRenderModule::RemoveTexture(int32_t index)
{
    if (index >= 0)
    {
        m_RTInfoTables.m_Textures[index].count -= 1;
        if (m_RTInfoTables.m_Textures[index].count == 0)
        {
            m_RTInfoTables.m_Textures[index].pTexture = nullptr;
        }
    }
}
