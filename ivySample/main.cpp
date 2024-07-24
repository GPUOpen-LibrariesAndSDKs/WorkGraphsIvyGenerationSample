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

// Framework and Windows implementation
#include "core/framework.h"
#include "core/win/framework_win.h"

// Config file parsing
#include "misc/fileio.h"

// Content manager to fix texture load bug
#include "core/contentmanager.h"

// Render Module Registry
#include "rendermoduleregistry.h"
// Render Modules
#include "ivyrendermodule.h"

// D3D12 header to enable experimental shader models
#include "d3d12.h"

using namespace cauldron;

class IvySample final : public Framework
{
public:
    IvySample(const FrameworkInitParams* pInitParams)
        : Framework(pInitParams)
    {
    }

    ~IvySample() = default;

    // Overrides
    void ParseSampleConfig() override
    {
        const auto configFileName = L"configs/ivysampleconfig.json";

        json sampleConfig;
        CauldronAssert(ASSERT_CRITICAL, ParseJsonFile(configFileName, sampleConfig), L"Could not parse JSON file %ls", configFileName);

        // Get the sample configuration
        json configData = sampleConfig["Ivy Generation Sample"];

        // Let the framework parse all the "known" options for us
        ParseConfigData(configData);
    }

    void RegisterSampleModules() override
    {
        // Init all pre-registered render modules
        rendermodule::RegisterAvailableRenderModules();

        // Register sample render module
        RenderModuleFactory::RegisterModule<IvyRenderModule>("IvyRenderModule");
    }
};

static FrameworkInitParamsInternal s_WindowsParams;

//////////////////////////////////////////////////////////////////////////
// WinMain
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    // Enable experimental D3D12 features for mesh nodes
    std::array<UUID, 2> meshNodesExperimentalFeatures = {D3D12ExperimentalShaderModels, D3D12StateObjectsExperiment};
    CauldronThrowOnFail(
        D3D12EnableExperimentalFeatures(static_cast<UINT>(meshNodesExperimentalFeatures.size()), meshNodesExperimentalFeatures.data(), nullptr, nullptr));

    // Create the sample and kick it off to the framework to run
    FrameworkInitParams initParams = {};
    initParams.Name                = L"Ivy Generation Sample";
    initParams.CmdLine             = lpCmdLine;
    initParams.AdditionalParams    = &s_WindowsParams;

    // Setup the windows info
    s_WindowsParams.InstanceHandle = hInstance;
    s_WindowsParams.CmdShow        = nCmdShow;

    IvySample frameworkInstance(&initParams);
    return RunFramework(&frameworkInstance);
}
