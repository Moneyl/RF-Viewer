#include "TerritoryDocument.h"
#include "render/backend/DX11Renderer.h"
#include "common/filesystem/Path.h"
#include "util/MeshUtil.h"
#include <RfgTools++\formats\zones\properties\primitive\StringProperty.h>
#include <RfgTools++\formats\textures\PegFile10.h>
#include "gui/documents/PegHelpers.h"
#include "Log.h"
#include <span>

TerritoryDocument::TerritoryDocument(GuiState* state, string territoryName, string territoryShortname)
    : TerritoryName(territoryName), TerritoryShortname(territoryShortname)
{
    state_ = state;

    //Create scene instance and store index
    state->Renderer->CreateScene();
    Scene = state->Renderer->Scenes.back();

    //Init scene camera
    Scene->Cam.Init({ -2573.0f, 200.0f, 963.0f }, 80.0f, { (f32)Scene->Width(), (f32)Scene->Height() }, 1.0f, 10000.0f);
    Scene->SetShader(terrainShaderPath_);
    Scene->SetVertexLayout
    ({
        { "POSITION", 0,  DXGI_FORMAT_R16G16B16A16_SINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0,  DXGI_FORMAT_R32G32B32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        });
    Scene->perFrameStagingBuffer_.DiffuseIntensity = 1.2f;

    //Create worker thread to load terrain meshes in background
    WorkerFuture = std::async(std::launch::async, &TerritoryDocument::WorkerThread, this, state);
}

TerritoryDocument::~TerritoryDocument()
{
    //Wait for worker thread to exit
    open_ = false;
    WorkerFuture.wait();
    WorkerThread_ClearData();

    if (state_->CurrentTerritory == &Territory)
    {
        state_->CurrentTerritory = nullptr;
        state_->SetSelectedZoneObject(nullptr);
    }

    //Free territory data
    Territory.ResetTerritoryData();

    //Delete scene and free its resources
    state_->Renderer->DeleteScene(Scene);
}

void TerritoryDocument::Update(GuiState* state)
{
    if (!ImGui::Begin(Title.c_str(), &open_))
    {
        ImGui::End();
        return;
    }

    //Only redraw scene if window is focused
    Scene->NeedsRedraw = ImGui::IsWindowFocused();
    if (WorkerDone) //Once worker thread is done clear its temporary data
    {
        if (!WorkerResourcesFreed && !NewTerrainInstanceAdded)
        {
            WorkerThread_ClearData();
            WorkerResourcesFreed = true;
        }
    }
    //Create dx11 resources for terrain meshes as they're loaded
    if (NewTerrainInstanceAdded)
    {
        std::lock_guard<std::mutex> lock(ResourceLock);
        //Create terrain index & vertex buffers
        for (auto& instance : TerrainInstances)
        {
            //Skip already-initialized terrain instances
            if (instance.RenderDataInitialized)
                continue;

            for (u32 i = 0; i < 9; i++)
            {
                auto& renderObject = Scene->Objects.emplace_back();
                Mesh mesh;
                mesh.Create(Scene->d3d11Device_, Scene->d3d11Context_, ToByteSpan(instance.Vertices[i]), ToByteSpan(instance.Indices[i]),
                    instance.Vertices[i].size(), DXGI_FORMAT_R16_UINT, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                renderObject.Create(mesh, instance.Position);

                if (instance.HasBlendTexture)
                {
                    //Create and setup texture2d
                    Texture2D texture2d;
                    texture2d.Name = Path::GetFileNameNoExtension(instance.Name) + "_alpha00.cvbm_pc";
                    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_BC1_UNORM;
                    D3D11_SUBRESOURCE_DATA textureSubresourceData;
                    textureSubresourceData.pSysMem = instance.BlendTextureBytes.data();
                    textureSubresourceData.SysMemSlicePitch = 0;
                    textureSubresourceData.SysMemPitch = PegHelpers::CalcRowPitch(dxgiFormat, instance.BlendTextureWidth, instance.BlendTextureHeight);
                    state->Renderer->ContextMutex.lock(); //Lock ID3D11DeviceContext mutex. Only one thread allowed to access it at once
                    texture2d.Create(Scene->d3d11Device_, instance.BlendTextureWidth, instance.BlendTextureHeight, dxgiFormat, D3D11_BIND_SHADER_RESOURCE, &textureSubresourceData);
                    texture2d.CreateShaderResourceView(); //Need shader resource view to use it in shader
                    texture2d.CreateSampler(); //Need sampler too
                    state->Renderer->ContextMutex.unlock();

                    renderObject.UseTextures = true;
                    renderObject.DiffuseTexture = texture2d;
                }
            }

            //Set bool so the instance isn't initialized more than once
            instance.RenderDataInitialized = true;
        }
        Scene->NeedsRedraw = true; //Redraw scene if new terrain meshes added
        NewTerrainInstanceAdded = false;
    }

    //Set current territory to most recently focused territory window
    if (ImGui::IsWindowFocused())
    {
        state->SetTerritory(TerritoryName);
        state->CurrentTerritory = &Territory;
        Scene->Cam.InputActive = true;
    }
    else
    {
        Scene->Cam.InputActive = false;
    }

    //Move camera if triggered by another gui panel
    if (state->CurrentTerritoryCamPosNeedsUpdate && &Territory == state->CurrentTerritory)
    {
        Vec3 newPos = state->CurrentTerritoryNewCamPos;
        Scene->Cam.SetPosition(newPos.x, newPos.y, newPos.z);
        Scene->Cam.LookAt({ newPos.x - 250.0f, newPos.y - 500.0f, newPos.z - 250.f });
        Scene->NeedsRedraw = true;
        state->CurrentTerritoryCamPosNeedsUpdate = false;
    }
    //Update debug draw regardless of focus state since we'll never be focused when using the other panels which control debug draw
    state->CurrentTerritoryUpdateDebugDraw = true; //Now set to true permanently to support time based coloring. Quick hack, probably should remove this variable later.
    if (Territory.ZoneFiles.size() != 0 && state->CurrentTerritoryUpdateDebugDraw && TerritoryDataLoaded)
    {
        UpdateDebugDraw(state);
        PrimitivesNeedRedraw = false;
    }

    ImVec2 contentAreaSize;
    contentAreaSize.x = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    contentAreaSize.y = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;

    Scene->HandleResize(contentAreaSize.x, contentAreaSize.y);

    //Store initial position so we can draw buttons over the scene texture after drawing it
    ImVec2 initialPos = ImGui::GetCursorPos();

    //Render scene texture
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Scene->ClearColor.x, Scene->ClearColor.y, Scene->ClearColor.z, Scene->ClearColor.w));
    ImGui::Image(Scene->GetView(), ImVec2(static_cast<f32>(Scene->Width()), static_cast<f32>(Scene->Height())));
    ImGui::PopStyleColor();

    //Set cursor pos to top left corner to draw buttons over scene texture
    ImVec2 adjustedPos = initialPos;
    adjustedPos.x += 10.0f;
    adjustedPos.y += 10.0f;
    ImGui::SetCursorPos(adjustedPos);

    DrawOverlayButtons(state);

    ImGui::End();
}

void TerritoryDocument::DrawOverlayButtons(GuiState* state)
{
    state->FontManager->FontL.Push();
    if (ImGui::Button(ICON_FA_CAMERA))
        ImGui::OpenPopup("##CameraSettingsPopup");
    state->FontManager->FontL.Pop();
    if (ImGui::BeginPopup("##CameraSettingsPopup"))
    {
        state->FontManager->FontL.Push();
        ImGui::Text("Camera");
        state->FontManager->FontL.Pop();
        ImGui::Separator();

        //If popup is visible then redraw scene each frame. Simpler than trying to add checks for each option changing
        Scene->NeedsRedraw = true;

        f32 fov = Scene->Cam.GetFovDegrees();
        f32 nearPlane = Scene->Cam.GetNearPlane();
        f32 farPlane = Scene->Cam.GetFarPlane();
        f32 lookSensitivity = Scene->Cam.GetLookSensitivity();

        if (ImGui::Button("0.1")) Scene->Cam.Speed = 0.1f;
        ImGui::SameLine();
        if (ImGui::Button("1.0")) Scene->Cam.Speed = 1.0f;
        ImGui::SameLine();
        if (ImGui::Button("10.0")) Scene->Cam.Speed = 10.0f;
        ImGui::SameLine();
        if (ImGui::Button("25.0")) Scene->Cam.Speed = 25.0f;
        ImGui::SameLine();
        if (ImGui::Button("50.0")) Scene->Cam.Speed = 50.0f;
        ImGui::SameLine();
        if (ImGui::Button("100.0")) Scene->Cam.Speed = 100.0f;

        ImGui::InputFloat("Speed", &Scene->Cam.Speed);
        ImGui::InputFloat("Sprint speed", &Scene->Cam.SprintSpeed);
        ImGui::Separator();

        if (ImGui::SliderFloat("Fov", &fov, 40.0f, 120.0f))
            Scene->Cam.SetFovDegrees(fov);
        if (ImGui::InputFloat("Near plane", &nearPlane))
            Scene->Cam.SetNearPlane(nearPlane);
        if (ImGui::InputFloat("Far plane", &farPlane))
            Scene->Cam.SetFarPlane(farPlane);
        if (ImGui::InputFloat("Look sensitivity", &lookSensitivity))
            Scene->Cam.SetLookSensitivity(lookSensitivity);

        ImGui::Separator();
        if (ImGui::InputFloat3("Position", (float*)&Scene->Cam.camPosition))
        {
            Scene->Cam.UpdateViewMatrix();
        }

        gui::LabelAndValue("Pitch:", std::to_string(Scene->Cam.GetPitchDegrees()));
        gui::LabelAndValue("Yaw:", std::to_string(Scene->Cam.GetYawDegrees()));

        ImGui::EndPopup();
    }

    ImGui::SameLine();
    state->FontManager->FontL.Push();
    if (ImGui::Button(ICON_FA_SUN))
        ImGui::OpenPopup("##SceneSettingsPopup");
    state->FontManager->FontL.Pop();
    if (ImGui::BeginPopup("##SceneSettingsPopup"))
    {
        state->FontManager->FontL.Push();
        ImGui::Text("Render settings");
        state->FontManager->FontL.Pop();
        ImGui::Separator();

        //If popup is visible then redraw scene each frame. Simpler than trying to add checks for each option changing
        Scene->NeedsRedraw = true;

        ImGui::Text("Shading mode: ");
        ImGui::SameLine();
        ImGui::RadioButton("Elevation", &Scene->perFrameStagingBuffer_.ShadeMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Diffuse", &Scene->perFrameStagingBuffer_.ShadeMode, 1);

        if (Scene->perFrameStagingBuffer_.ShadeMode != 0)
        {
            ImGui::Text("Diffuse presets: ");
            ImGui::SameLine();
            if (ImGui::Button("Default"))
            {
                Scene->perFrameStagingBuffer_.DiffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                Scene->perFrameStagingBuffer_.DiffuseIntensity = 1.2;
                Scene->perFrameStagingBuffer_.ElevationFactorBias = 0.8f;
            }

            ImGui::ColorEdit3("Diffuse", reinterpret_cast<f32*>(&Scene->perFrameStagingBuffer_.DiffuseColor));
            ImGui::SliderFloat("Diffuse intensity", &Scene->perFrameStagingBuffer_.DiffuseIntensity, 0.0f, 2.0f);
        }

        ImGui::EndPopup();
    }
}

void TerritoryDocument::UpdateDebugDraw(GuiState* state)
{
    //Reset primitives first to ensure old primitives get cleared
    Scene->ResetPrimitives();

    //Draw bounding boxes
    for (const auto& zone : Territory.ZoneFiles)
    {
        if (!zone.RenderBoundingBoxes)
            continue;

        for (const auto& object : zone.Zone.Objects)
        {
            auto objectClass = Territory.GetObjectClass(object.ClassnameHash);
            if (!objectClass.Show)
                continue;

            //If object is selected in zone object list panel use different drawing method for visibilty
            bool selectedInZoneObjectList = &object == state->ZoneObjectList_SelectedObject;
            if (selectedInZoneObjectList)
            {
                //Calculate color that changes with time
                Vec3 color = objectClass.Color;
                f32 colorMagnitude = objectClass.Color.Magnitude();
                //Negative values used for brighter colors so they get darkened instead of lightened//Otherwise doesn't work on objects with white debug color
                f32 multiplier = colorMagnitude > 0.85f ? -1.0f : 1.0f;
                color.x = objectClass.Color.x + powf(sin(Scene->TotalTime * 2.0f), 2.0f) * multiplier;
                color.y = objectClass.Color.y + powf(sin(Scene->TotalTime), 2.0f) * multiplier;
                color.z = objectClass.Color.z + powf(sin(Scene->TotalTime), 2.0f) * multiplier;

                //Keep color in a certain range so it stays visible against the terrain
                f32 magnitudeMin = 0.20f;
                f32 colorMin = 0.20f;
                if (color.Magnitude() < magnitudeMin)
                {
                    color.x = std::max(color.x, colorMin);
                    color.y = std::max(color.y, colorMin);
                    color.z = std::max(color.z, colorMin);
                }

                //Calculate bottom center of box so we can draw a line from the bottom of the box into the sky
                Vec3 lineStart;
                lineStart.x = (object.Bmin.x + object.Bmax.x) / 2.0f;
                lineStart.y = object.Bmin.y;
                lineStart.z = (object.Bmin.z + object.Bmax.z) / 2.0f;
                Vec3 lineEnd = lineStart;
                lineEnd.y += 300.0f;

                //Draw object bounding box and line from it's bottom into the sky
                Scene->DrawBox(object.Bmin, object.Bmax, color);
                Scene->DrawLine(lineStart, lineEnd, color);
            }
            else //If not selected just draw bounding box with static color
            {
                Scene->DrawBox(object.Bmin, object.Bmax, objectClass.Color);
            }
        }
    }

    Scene->NeedsRedraw = true;
    state->CurrentTerritoryUpdateDebugDraw = false;
}

void TerritoryDocument::WorkerThread(GuiState* state)
{
    //Read all zones from zonescript_terr01.vpp_pc
    state->SetStatus(ICON_FA_SYNC " Loading zones for " + Title, Working);
    Territory.Init(state->PackfileVFS, TerritoryName, TerritoryShortname);
    Territory.LoadZoneData();
    state->CurrentTerritoryUpdateDebugDraw = true;
    TerritoryDataLoaded = true;

    std::vector<ZoneData>& zoneFiles = Territory.ZoneFiles;
    Log->info("Loaded {} zones for {}", zoneFiles.size(), Title);

    //Move camera close to zone with the most objects by default. Convenient as some territories have origins distant from each other
    if (zoneFiles.size() > 0 && zoneFiles[0].Zone.Objects.size() > 0)
    {
        //Tell camera to move to near the first object in the zone
        state->CurrentTerritoryNewCamPos = zoneFiles[0].Zone.Objects[0].Bmin + Vec3(250.0f, 500.0f, 250.0f);
        state->CurrentTerritoryCamPosNeedsUpdate = true;
    }

    //End worker thread early if document is closed
    if (!open_)
    {
        state->ClearStatus();
        return;
    }

    //Load terrain meshes and extract their index + vertex data
    WorkerThread_LoadTerrainMeshes(state);
    state->ClearStatus();
    WorkerDone = true;
}

void TerritoryDocument::WorkerThread_ClearData()
{
    Log->info("Temporary data cleared for {} terrain worker threads", Title);
    for (auto& instance : TerrainInstances)
    {
        //Free vertex and index buffer memory
        //Note: Assumes same amount of vertex and index buffers
        for (u32 i = 0; i < instance.Indices.size(); i++)
        {
            if(instance.Indices[i].data())
                delete instance.Indices[i].data();
            if(instance.Vertices[i].data())
                delete instance.Vertices[i].data();
        }
        //Clear vectors
        instance.Indices.clear();
        instance.Vertices.clear();
        instance.Meshes.clear();

        //Clear blend texture data
        if (instance.HasBlendTexture)
        {
            //Cleanup peg texture data
            instance.BlendPeg.Cleanup();
            //Set to nullptr and 0. Don't have to delete this since it's really referencing the data owned by BlendPeg
            instance.BlendTextureBytes = std::span<u8>((u8*)nullptr, 0);
        }
    }
    //Clear instance list
    TerrainInstances.clear();
}

void TerritoryDocument::WorkerThread_LoadTerrainMeshes(GuiState* state)
{
    state->SetStatus(ICON_FA_SYNC " Loading terrain meshes for " + Title, Working);

    //Must store futures for std::async to run functions asynchronously
    std::vector<std::future<void>> futures;

    //Find terrain meshes and load them
    for (auto& zone : Territory.ZoneFiles)
    {
        //Exit early if document closes
        if (!open_)
            break;

        //Get obj_zone object with a terrain_file_name property
        auto* objZoneObject = zone.Zone.GetSingleObject("obj_zone");
        if (!objZoneObject)
            continue;
        auto* terrainFilenameProperty = objZoneObject->GetProperty<StringProperty>("terrain_file_name");
        if (!terrainFilenameProperty)
            continue;

        //Remove extra null terminators that RFG so loves to have in it's files
        string filename = terrainFilenameProperty->Data;
        if (filename.ends_with('\0'))
            filename.pop_back();

        //Exit early if document closes
        if (!open_)
            break;

        filename += ".cterrain_pc";
        Vec3 position = objZoneObject->Bmin + ((objZoneObject->Bmax - objZoneObject->Bmin) / 2.0f);
        auto terrainMeshHandleCpu = state->PackfileVFS->GetFiles(filename, true, true);
        if (terrainMeshHandleCpu.size() > 0)
            futures.push_back(std::async(std::launch::async, &TerritoryDocument::WorkerThread_LoadTerrainMesh, this, terrainMeshHandleCpu[0], position, state));
    }

    //Wait for all threads to exit
    for (auto& future : futures)
        future.wait();

    Log->info("Done loading terrain meshes for {}", Title);
}

void TerritoryDocument::WorkerThread_LoadTerrainMesh(FileHandle terrainMesh, Vec3 position, GuiState* state)
{
    //Get packfile that holds terrain meshes
    auto* container = terrainMesh.GetContainer();
    if (!container)
        THROW_EXCEPTION("Failed to get container pointer for a terrain mesh.");

    //Todo: This does a full extract twice on the container due to the way single file extracts work. Fix this
    //Get mesh file byte arrays
    auto cpuFileBytes = container->ExtractSingleFile(terrainMesh.Filename(), true);
    auto gpuFileBytes = container->ExtractSingleFile(Path::GetFileNameNoExtension(terrainMesh.Filename()) + ".gterrain_pc", true);

    //Ensure the mesh files were extracted
    if (!cpuFileBytes)
        THROW_EXCEPTION("Failed to extract terrain mesh cpu file.");
    if (!gpuFileBytes)
        THROW_EXCEPTION("Failed to extract terrain mesh gpu file.");

    BinaryReader cpuFile(cpuFileBytes.value());
    BinaryReader gpuFile(gpuFileBytes.value());

    //Create new instance
    TerrainInstance terrain;
    terrain.Position = position;

    //Get vertex data. Each terrain file is made up of 9 meshes which are stitched together
    u32 cpuFileIndex = 0;
    u32* cpuFileAsUintArray = (u32*)cpuFileBytes.value().data();
    for (u32 i = 0; i < 9; i++)
    {
        //Exit early if document closes
        if (!open_)
            break;

        //Get mesh crc from gpu file. Will use this to find the mesh description data section of the cpu file which starts and ends with this value
        //In while loop since a mesh file pair can have multiple meshes inside
        u32 meshCrc = gpuFile.ReadUint32();
        if (meshCrc == 0)
            THROW_EXCEPTION("Failed to read next mesh data block hash in terrain mesh gpu file.");

        //Find next mesh data block in cpu file
        while (true)
        {
            //This is done instead of using BinaryReader::ReadUint32() because that method was incredibly slow (+ several minutes slow)
            if (cpuFileAsUintArray[cpuFileIndex] == meshCrc)
                break;

            cpuFileIndex++;
        }
        u64 meshDataBlockStart = (cpuFileIndex * 4) - 4;
        cpuFile.SeekBeg(meshDataBlockStart);

        //Read mesh data block. Contains info on vertex + index layout + size + format
        MeshDataBlock meshData;
        meshData.Read(cpuFile);
        cpuFileIndex += static_cast<u32>(cpuFile.Position() - meshDataBlockStart) / 4;
        terrain.Meshes.push_back(meshData);

        //Read index data
        gpuFile.Align(16); //Indices always start here
        u32 indicesSize = meshData.NumIndices * meshData.IndexSize;
        u8* indexBuffer = new u8[indicesSize];
        gpuFile.ReadToMemory(indexBuffer, indicesSize);
        terrain.Indices.push_back(std::span<u16>{ (u16*)indexBuffer, indicesSize / meshData.IndexSize });

        //Read vertex data
        gpuFile.Align(16);
        u32 verticesSize = meshData.NumVertices * meshData.VertexStride0;
        u8* vertexBuffer = new u8[verticesSize];
        gpuFile.ReadToMemory(vertexBuffer, verticesSize);

        //Exit early if document closes
        if (!open_)
            break;

        std::span<LowLodTerrainVertex> verticesWithNormals = WorkerThread_GenerateTerrainNormals
        (
            std::span<ShortVec4>{ (ShortVec4*)vertexBuffer, verticesSize / meshData.VertexStride0},
            std::span<u16>{ (u16*)indexBuffer, indicesSize / meshData.IndexSize }
        );
        terrain.Vertices.push_back(verticesWithNormals);

        //Free vertex buffer, no longer need this copy. verticesWithNormals copied the data it needed from this one
        delete[] vertexBuffer;

        u32 endMeshCrc = gpuFile.ReadUint32();
        if (meshCrc != endMeshCrc)
            THROW_EXCEPTION("Verification hashes at the start and end of terrain gpu file don't match.");
    }

    //Clear resources
    delete container;
    delete[] cpuFileBytes.value().data();
    delete[] gpuFileBytes.value().data();

    //Exit early if document closes
    if (!open_)
        return;

    //Todo: Use + "_alpha00" here to get the blend weights texture, load high res textures, and apply those. Will make terrain texture higher res and have specular + normal maps
    //Todo: Remember to also change the DXGI_FORMAT for the Texture2D to DXGI_FORMAT_R8G8B8A8_UNORM since that's what the _alpha00 textures used instead of DXT1
    //Get terrain blending texture
    string blendTextureName = Path::GetFileNameNoExtension(terrainMesh.Filename()) + "comb.cvbm_pc";
    auto blendTextureHandlesCpu = state->PackfileVFS->GetFiles(blendTextureName, true, true);
    if (blendTextureHandlesCpu.size() > 0)
    {
        FileHandle& blendTextureHandle = blendTextureHandlesCpu[0];
        auto* containerBlend = blendTextureHandle.GetContainer();
        if (!containerBlend)
            THROW_EXCEPTION("Failed to get container pointer for a terrain mesh.");

        //Get mesh file byte arrays
        auto cpuFileBytesBlend = containerBlend->ExtractSingleFile(blendTextureName, true);
        auto gpuFileBytesBlend = containerBlend->ExtractSingleFile(Path::GetFileNameNoExtension(blendTextureName) + ".gvbm_pc", true);

        //Ensure the texture files were extracted
        if (!cpuFileBytesBlend)
            THROW_EXCEPTION("Failed to extract terrain mesh cpu file.");
        if (!gpuFileBytesBlend)
            THROW_EXCEPTION("Failed to extract terrain mesh gpu file.");

        BinaryReader cpuFileBlend(cpuFileBytesBlend.value());
        BinaryReader gpuFileBlend(gpuFileBytesBlend.value());

        terrain.BlendPeg.Read(cpuFileBlend, gpuFileBlend);
        terrain.BlendPeg.ReadTextureData(gpuFileBlend, terrain.BlendPeg.Entries[0]);
        auto maybeBlendTexturePixelData = terrain.BlendPeg.GetTextureData(0);
        if (maybeBlendTexturePixelData)
        {
            terrain.HasBlendTexture = true;
            terrain.BlendTextureBytes = maybeBlendTexturePixelData.value();
            terrain.BlendTextureWidth = terrain.BlendPeg.Entries[0].Width;
            terrain.BlendTextureHeight = terrain.BlendPeg.Entries[0].Height;
        }
        else
        {
            Log->warn("Failed to extract pixel data for terrain blend texture {}", blendTextureName);
        }

        delete containerBlend;
        delete[] cpuFileBytesBlend.value().data();
        delete[] gpuFileBytesBlend.value().data();
    }
    else
    {
        Log->warn("Couldn't find blend texture for {}.", terrainMesh.Filename());
    }

    //Acquire resource lock before writing terrain instance data to the instance list
    std::lock_guard<std::mutex> lock(ResourceLock);
    TerrainInstances.push_back(terrain);
    NewTerrainInstanceAdded = true;
}

std::span<LowLodTerrainVertex> TerritoryDocument::WorkerThread_GenerateTerrainNormals(std::span<ShortVec4> vertices, std::span<u16> indices)
{
    struct Face
    {
        u32 verts[3];
    };

    //Generate list of faces and face normals
    std::vector<Face> faces = {};
    for (u32 i = 0; i < indices.size() - 3; i++)
    {
        u32 index0 = indices[i];
        u32 index1 = indices[i + 1];
        u32 index2 = indices[i + 2];

        faces.emplace_back(Face{ .verts = {index0, index1, index2} });
    }

    //Exit early if document closes
    if (!open_)
        return std::span<LowLodTerrainVertex>();

    //Generate list of vertices with position and normal data
    u8* vertBuffer = new u8[vertices.size() * sizeof(LowLodTerrainVertex)];
    std::span<LowLodTerrainVertex> outVerts((LowLodTerrainVertex*)vertBuffer, vertices.size());
    for (u32 i = 0; i < vertices.size(); i++)
    {
        outVerts[i].x = vertices[i].x;
        outVerts[i].y = vertices[i].y;
        outVerts[i].z = vertices[i].z;
        outVerts[i].w = vertices[i].w;
        outVerts[i].normal = { 0.0f, 0.0f, 0.0f };
    }
    for (auto& face : faces)
    {
        //Exit early if document closes
        if (!open_)
            return outVerts;

        const u32 ia = face.verts[0];
        const u32 ib = face.verts[1];
        const u32 ic = face.verts[2];

        Vec3 vert0 = { (f32)vertices[ia].x, (f32)vertices[ia].y, (f32)vertices[ia].z };
        Vec3 vert1 = { (f32)vertices[ib].x, (f32)vertices[ib].y, (f32)vertices[ib].z };
        Vec3 vert2 = { (f32)vertices[ic].x, (f32)vertices[ic].y, (f32)vertices[ic].z };

        Vec3 e1 = vert1 - vert0;
        Vec3 e2 = vert2 - vert1;
        Vec3 normal = e1.Cross(e2);

        //Todo: Make sure this isn't subtly wrong
        //Attempt to flip normal if it's pointing in wrong direction. Seems to result in correct normals
        if (normal.y < 0.0f)
        {
            normal.x *= -1.0f;
            normal.y *= -1.0f;
            normal.z *= -1.0f;
        }
        outVerts[ia].normal += normal;
        outVerts[ib].normal += normal;
        outVerts[ic].normal += normal;
    }
    for (u32 i = 0; i < vertices.size(); i++)
    {
        outVerts[i].normal = outVerts[i].normal.Normalize();
    }
    return outVerts;
}