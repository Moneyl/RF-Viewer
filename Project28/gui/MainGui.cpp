#include "MainGui.h"
#include "common/Typedefs.h"
#include "common/filesystem/Path.h"
#include "render/imgui/ImGuiFontManager.h"
#include "rfg/PackfileVFS.h"
#include "render/imgui/imgui_ext.h"
#include "render/camera/Camera.h"
#include <imgui/imgui.h>
#include <im3d.h>
#include <im3d_math.h>
#include <filesystem>
#include <iostream>

MainGui::MainGui(ImGuiFontManager* fontManager, PackfileVFS* packfileVFS, Camera* camera, HWND hwnd) 
    : fontManager_(fontManager), packfileVFS_(packfileVFS), camera_(camera), hwnd_(hwnd) 
{
    //Todo: Make this load them directly from the packfile instead and search in other packfiles & str2s
    //Read all zone files in pre-extracted zone folder
    for (auto& file : std::filesystem::directory_iterator("G:/RFG Unpack/data/Unpack/zonescript_terr01.vpp_pc/"))
    {
        if (Path::GetExtension(file) != ".rfgzone_pc")
            continue;

        BinaryReader reader(file.path().string());
        ZoneFile zoneFile;
        zoneFile.Name = Path::GetFileName(file);
        zoneFile.Zone.Read(reader);
        zoneFiles_.push_back(zoneFile);
    }

    //Sort vector by object count for convenience
    std::sort(zoneFiles_.begin(), zoneFiles_.end(),
    [](const ZoneFile& a, const ZoneFile& b)
    {
        return a.Zone.Header.NumObjects > b.Zone.Header.NumObjects;
    });

    //Make first zone visible for convenience when debugging
    zoneFiles_[0].RenderBoundingBoxes = true;
    //Init object class data used for filtering and labelling
    InitObjectClassData();
    //Select first zone by default
    SetSelectedZone(0);
}

void MainGui::Update(f32 deltaTime)
{
    //Run gui code
    DrawMainMenuBar();
    DrawDockspace();
    DrawCameraWindow();
    DrawFileExplorer();
    DrawZoneWindow();
    DrawZoneObjectsWindow();
    DrawZonePrimitives();
    DrawIm3dPrimitives();
}

void MainGui::HandleResize()
{
    RECT usableRect;

    if (GetClientRect(hwnd_, &usableRect))
    {
        windowWidth_ = usableRect.right - usableRect.left;
        windowHeight_ = usableRect.bottom - usableRect.top;
    }
}

void MainGui::DrawMainMenuBar()
{
    //Todo: Make this actually work
    ImGuiMainMenuBar
    (
        ImGuiMenu("File",
            ImGuiMenuItemShort("Open file", )
            ImGuiMenuItemShort("Save file", )
            ImGuiMenuItemShort("Exit", )
        )
        ImGuiMenu("Help",
            ImGuiMenuItemShort("Welcome", )
            ImGuiMenuItemShort("Metrics", )
            ImGuiMenuItemShort("About", )
        )
    )
}

void MainGui::DrawDockspace()
{
    //Dockspace flags
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    //Parent window flags
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetWorkPos());
    ImGui::SetNextWindowSize(viewport->GetWorkSize());
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace parent window", &Visible, window_flags);
    ImGui::PopStyleVar(3);

    // DockSpace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("Editor dockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    ImGui::End();
}

void MainGui::DrawFileExplorer()
{
    ImGui::Begin("File explorer");

    fontManager_->FontL.Push();
    ImGui::Text(ICON_FA_ARCHIVE " Packfiles");
    fontManager_->FontL.Pop();
    ImGui::Separator();
    for (auto& packfile : packfileVFS_->packfiles_)
    {
        string packfileNodeLabel = packfile.Name() + " [" + std::to_string(packfile.Header.NumberOfSubfiles) + " subfiles";
        if (packfile.Compressed)
            packfileNodeLabel += ", Compressed";
        if (packfile.Condensed)
            packfileNodeLabel += ", Condensed";

        packfileNodeLabel += "]";

        if (ImGui::TreeNode(packfileNodeLabel.c_str()))
        {
            for (u32 i = 0; i < packfile.Entries.size(); i++)
            {
                ImGui::BulletText("%s - %d bytes", packfile.EntryNames[i], packfile.Entries[i].DataSize);
            }
            ImGui::TreePop();
        }
    }

    ImGui::Separator();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
}

void MainGui::DrawZoneWindow()
{
    if (!ImGui::Begin("Zones", &Visible))
    {
        ImGui::End();
        return;
    }

    fontManager_->FontL.Push();
    ImGui::Text(ICON_FA_PALETTE " Render settings");
    fontManager_->FontL.Pop();
    ImGui::Separator();

    ImGui::ColorEdit4("Bounding box color", boundingBoxColor_);
    ImGui::SliderFloat("Bounding box thickness", &boundingBoxThickness_, 0.0f, 16.0f);

    ImGui::Separator();
    fontManager_->FontL.Push();
    ImGui::Text(ICON_FA_MAP " Zones");
    fontManager_->FontL.Pop();
    ImGui::Separator();
    
    static bool hideEmptyZones = true;
    ImGui::Checkbox("Hide empty zones", &hideEmptyZones);
    static bool hideObjectBelowObjectThreshold = true;
    ImGui::Checkbox("Hide objects below count", &hideObjectBelowObjectThreshold);
    static u32 minObjectsToShowZone = 2;
    if(hideObjectBelowObjectThreshold)
        ImGui::InputScalar("Min objects to show zone", ImGuiDataType_U32, &minObjectsToShowZone);

    ImGui::BeginChild("##Zone file list", ImVec2(0, 0), true);
    ImGui::Columns(2);
    u32 i = 0;
    for (auto& zone : zoneFiles_)
    {
        if (hideEmptyZones && zone.Zone.Header.NumObjects == 0 || !(hideObjectBelowObjectThreshold ? zone.Zone.Objects.size() >= minObjectsToShowZone : true))
        {
            i++;
            continue;
        }

        ImGui::SetColumnWidth(0, 200.0f);
        ImGui::SetColumnWidth(1, 300.0f);
        if (ImGui::Selectable(zone.Name.c_str()))
        {
            SetSelectedZone(i);
        }
        ImGui::NextColumn();
        ImGui::Checkbox((string("Draw##") + zone.Name).c_str(), &zone.RenderBoundingBoxes);
        ImGui::SameLine();
        if (ImGui::Button((string(ICON_FA_MAP_MARKER "##") + zone.Name).c_str()))
        {
            if (zone.Zone.Objects.size() > 0)
            {
                auto& firstObj = zone.Zone.Objects[0];
                Vec3 newCamPos = firstObj.Bmin;
                newCamPos.x += 50.0f;
                newCamPos.y += 100.0f;
                newCamPos.z += 50.0f;
                camera_->SetPosition(newCamPos.x, newCamPos.y, newCamPos.z);
            }
        }
        ImGui::SameLine();
        ImGui::Text(" | %d objects | %s", zone.Zone.Header.NumObjects, zone.Zone.DistrictName() == "unknown" ? "" : zone.Zone.DistrictNameCstr());
        ImGui::NextColumn();
        i++;
    }
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::End();
}

//Todo: May want to move this to be under each zone in the zone window list since we'll have a third panel for per-object properties
void MainGui::DrawZoneObjectsWindow()
{
    if (!ImGui::Begin("Zone objects", &Visible))
    {
        ImGui::End();
        return;
    }

    if (selectedZone == InvalidZoneIndex || selectedZone >= zoneFiles_.size())
    {
        ImGui::Text("%s Select a zone to see the objects it contains.", ICON_FA_EXCLAMATION_CIRCLE);
    }
    else
    {
        fontManager_->FontL.Push();
        ImGui::Text(ICON_FA_FILTER " Filtering");
        fontManager_->FontL.Pop();
        ImGui::Separator();

        ImGui::BeginChild("##Zone object filters list", ImVec2(0, 300.0f), true);
        for (auto& objectClass : zoneObjectClasses_)
        {
            ImGui::Checkbox(objectClass.Name.c_str(), &objectClass.Show);
            ImGui::SameLine();
            ImGui::TextColored(gui::SecondaryTextColor, "|  %d objects", objectClass.NumInstances);
            ImGui::SameLine();
            //Todo: Use a proper string formatting lib here
            ImGui::ColorEdit3(string("##" + objectClass.Name).c_str(), (f32*)&objectClass.Color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            //ImGui::Text("|  %d instances", objectClass.NumInstances);
        }
        ImGui::EndChild();

        ImGui::Separator();
        fontManager_->FontL.Push();
        ImGui::Text(ICON_FA_BOXES " Zone objects");
        fontManager_->FontL.Pop();
        ImGui::Separator();

        //Object list
        auto& zone = zoneFiles_[selectedZone].Zone;
        ImGui::BeginChild("##Zone object list", ImVec2(0, 0), true);
        ImGui::Columns(2);
        for (auto& object : zone.Objects)
        {
            if (!ShouldShowObjectClass(object.ClassnameHash))
                continue;

            Vec3 position = object.Bmax - object.Bmin;
            ImGui::SetColumnWidth(0, 200.0f);
            ImGui::SetColumnWidth(1, 300.0f);
            ImGui::Selectable(object.Classname.c_str());
            ImGui::NextColumn();
            ImGui::Text(" | {%.3f, %.3f, %.3f}", position.x, position.y, position.z);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::EndChild();
    }


    ImGui::End();
}

void MainGui::DrawZonePrimitives()
{
    Im3d::PushDrawState();
    Im3d::SetSize(boundingBoxThickness_);
    Im3d::SetColor(Im3d::Color(boundingBoxColor_.x, boundingBoxColor_.y, boundingBoxColor_.z, boundingBoxColor_.w));
    
    //Draw bounding boxes
    for (const auto& zone : zoneFiles_)
    {
        if (!zone.RenderBoundingBoxes)
            continue;

        for (const auto& object : zone.Zone.Objects)
        {
            auto objectClass = GetObjectClass(object.ClassnameHash);
            if (!objectClass.Show)
                continue;

            //Todo: Make conversion operators to simplify this
            Im3d::SetColor(Im3d::Color(objectClass.Color.x, objectClass.Color.y, objectClass.Color.z, objectClass.Color.w));
            Im3d::DrawAlignedBox(Im3d::Vec3(object.Bmin.x, object.Bmin.y, object.Bmin.z), Im3d::Vec3(object.Bmax.x, object.Bmax.y, object.Bmax.z));
        }
    }

    //Draw 0.0 height grid
    Im3d::SetAlpha(1.0f);
    Im3d::SetSize(1.0f);
    Im3d::BeginLines();

    //Update grid origin if it should follow the camera. Don't change y, just x & z coords
    if (gridFollowCamera_)
        gridOrigin_ = Im3d::Vec3(camera_->Position().m128_f32[0], gridOrigin_.y, camera_->Position().m128_f32[2]);

    if (drawGrid_)
    {
        const float gridHalf = (float)gridSize_ * 0.5f;
        for (int x = 0; x <= gridSize_; x += gridSpacing_)
        {
            Im3d::Vertex(gridOrigin_.x + -gridHalf, gridOrigin_.y + 0.0f, gridOrigin_.z + (float)x - gridHalf, Im3d::Color(0.0f, 0.0f, 0.0f));
            Im3d::Vertex(gridOrigin_.x + gridHalf, gridOrigin_.y + 0.0f, gridOrigin_.z + (float)x - gridHalf, Im3d::Color(1.0f, 1.0f, 1.0f));
        }
        for (int z = 0; z <= gridSize_; z += gridSpacing_)
        {
            Im3d::Vertex(gridOrigin_.x + (float)z - gridHalf, gridOrigin_.y + 0.0f, gridOrigin_.z + -gridHalf, Im3d::Color(0.0f, 0.0f, 0.0f));
            Im3d::Vertex(gridOrigin_.x + (float)z - gridHalf, gridOrigin_.y + 0.0f, gridOrigin_.z + gridHalf, Im3d::Color(1.0f, 1.0f, 1.0f));
        }
    }
    Im3d::End();

    Im3d::PopDrawState();
}

void MainGui::DrawCameraWindow()
{
    if (!ImGui::Begin("Camera", &Visible))
    {
        ImGui::End();
        return;
    }

    fontManager_->FontL.Push();
    ImGui::Text(ICON_FA_CAMERA " Camera");
    fontManager_->FontL.Pop();
    ImGui::Separator();

    f32 fov = camera_->GetFov();
    f32 nearPlane = camera_->GetNearPlane();
    f32 farPlane = camera_->GetFarPlane();
    f32 lookSensitivity = camera_->GetLookSensitivity();

    ImGui::InputFloat("Speed", &camera_->Speed);
    ImGui::InputFloat("Sprint speed", &camera_->SprintSpeed);
    //ImGui::InputFloat("Camera smoothing", &camera_->CameraSmoothing);
    ImGui::Separator();

    if (ImGui::InputFloat("Fov", &fov))
        camera_->SetFov(fov);
    if (ImGui::InputFloat("Near plane", &nearPlane))
        camera_->SetNearPlane(nearPlane);
    if (ImGui::InputFloat("Far plane", &farPlane))
        camera_->SetFarPlane(farPlane);
    if (ImGui::InputFloat("Look sensitivity", &lookSensitivity))
        camera_->SetLookSensitivity(lookSensitivity);

    //ImGui::Checkbox("Follow mouse", &camera_->FollowMouse);
    ImGui::Separator();

    if (ImGui::InputFloat3("Position", (float*)&camera_->camPosition, 3))
    {
        camera_->UpdateViewMatrix();
    }
    //gui::LabelAndValue("Position: ", util::to_string(camera_->GetPos()));
    //gui::LabelAndValue("Target position: ", util::to_string(camera_->GetTargetPos()));
    gui::LabelAndValue("Aspect ratio: ", std::to_string(camera_->GetAspectRatio()));
    gui::LabelAndValue("Pitch: ", std::to_string(camera_->GetPitch()));
    gui::LabelAndValue("Yaw: ", std::to_string(camera_->GetYaw()));

    ImGui::End();
}

void MainGui::DrawIm3dPrimitives()
{
    ImGui::Begin("Im3d tester");

    Im3d::Context& ctx = Im3d::GetContext();
    Im3d::AppData& ad = Im3d::GetAppData();

    ImGui::InputFloat3("Im3d ray origin", (float*)&ad.m_cursorRayOrigin);
    ImGui::InputFloat3("Im3d ray direction", (float*)&ad.m_cursorRayDirection);

    //Draw grid
    ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Grid"))
    {
        ImGui::SliderInt("Grid size", &gridSize_, 1, 10000);
        ImGui::SliderInt("Grid spacing", &gridSpacing_, 1, 50);
        ImGui::SliderFloat3("Grid origin", (float*)&gridOrigin_, -8192.0f, 8192.0f);
        ImGui::Checkbox("Have grid follow camera", &gridFollowCamera_);
        ImGui::Checkbox("Draw grid", &drawGrid_);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Cursor Ray Intersection"))
    {
        // Context exposes the 'hot depth' along the cursor ray which intersects with the current hot gizmo - this is useful
        // when drawing the cursor ray.
        float depth = FLT_MAX;
        depth = Im3d::Min(depth, Im3d::GetContext().m_hotDepth);
        float size = Im3d::Clamp(32.0f / depth, 4.0f, 32.0f);

        if (depth != FLT_MAX)
        {
            ImGui::Text("Depth: %f", depth);
            Im3d::PushEnableSorting(true);
            Im3d::BeginPoints();
            Im3d::Vertex(ad.m_cursorRayOrigin + ad.m_cursorRayDirection * depth * 0.99f, size, Im3d::Color_Magenta);
            Im3d::End();
            Im3d::PopEnableSorting();
        }
        else
        {
            ImGui::Text("Depth: FLT_MAX");
        }

        ImGui::TreePop();
    }

    //ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("High Order Shapes"))
    {
        // Im3d provides functions to easily draw high order shapes - these don't strictly require a matrix to be pushed on
        // the stack (although this is supported, as below).
        static Im3d::Mat4 transform(1.0f);
        Im3d::Gizmo("ShapeGizmo", transform);

        enum Shape {
            Shape_Quad,
            Shape_QuadFilled,
            Shape_Circle,
            Shape_CircleFilled,
            Shape_Sphere,
            Shape_SphereFilled,
            Shape_AlignedBox,
            Shape_AlignedBoxFilled,
            Shape_Cylinder,
            Shape_Capsule,
            Shape_Prism,
            Shape_Arrow
        };
        static const char* shapeList =
            "Quad\0"
            "Quad Filled\0"
            "Circle\0"
            "Circle Filled\0"
            "Sphere\0"
            "Sphere Filled\0"
            "AlignedBox\0"
            "AlignedBoxFilled\0"
            "Cylinder\0"
            "Capsule\0"
            "Prism\0"
            "Arrow\0"
            ;
        static int currentShape = Shape_Capsule;
        ImGui::Combo("Shape", &currentShape, shapeList);
        static Im3d::Vec4 color = Im3d::Vec4(1.0f, 0.0f, 0.6f, 1.0f);
        ImGui::ColorEdit4("Color", color);
        static float thickness = 4.0f;
        ImGui::SliderFloat("Thickness", &thickness, 0.0f, 16.0f);
        static int detail = -1;

        Im3d::PushMatrix(transform);
        Im3d::PushDrawState();
        Im3d::SetSize(thickness);
        Im3d::SetColor(Im3d::Color(color.x, color.y, color.z, color.w));

        switch ((Shape)currentShape)
        {
        case Shape_Quad:
        case Shape_QuadFilled:
        {
            static Im3d::Vec2 quadSize(1.0f);
            ImGui::SliderFloat2("Size", quadSize, 0.0f, 10.0f);
            if (currentShape == Shape_Quad)
            {
                Im3d::DrawQuad(Im3d::Vec3(0.0f), Im3d::Vec3(0.0f, 0.0f, 1.0f), quadSize);
            }
            else
            {
                Im3d::DrawQuadFilled(Im3d::Vec3(0.0f), Im3d::Vec3(0.0f, 0.0f, 1.0f), quadSize);
            }
            break;
        }
        case Shape_Circle:
        case Shape_CircleFilled:
        {
            static float circleRadius = 1.0f;
            ImGui::SliderFloat("Radius", &circleRadius, 0.0f, 10.0f);
            ImGui::SliderInt("Detail", &detail, -1, 128);
            if (currentShape == Shape_Circle)
            {
                Im3d::DrawCircle(Im3d::Vec3(0.0f), Im3d::Vec3(0.0f, 0.0f, 1.0f), circleRadius, detail);
            }
            else if (currentShape = Shape_CircleFilled)
            {
                Im3d::DrawCircleFilled(Im3d::Vec3(0.0f), Im3d::Vec3(0.0f, 0.0f, 1.0f), circleRadius, detail);
            }
            break;
        }
        case Shape_Sphere:
        case Shape_SphereFilled:
        {
            static float sphereRadius = 1.0f;
            ImGui::SliderFloat("Radius", &sphereRadius, 0.0f, 10.0f);
            ImGui::SliderInt("Detail", &detail, -1, 128);
            if (currentShape == Shape_Sphere)
            {
                Im3d::DrawSphere(Im3d::Vec3(0.0f), sphereRadius, detail);
            }
            else
            {
                Im3d::DrawSphereFilled(Im3d::Vec3(0.0f), sphereRadius, detail);
            }
            break;
        }
        case Shape_AlignedBox:
        case Shape_AlignedBoxFilled:
        {
            static Im3d::Vec3 boxSize(1.0f);
            ImGui::SliderFloat3("Size", boxSize, 0.0f, 10.0f);
            if (currentShape == Shape_AlignedBox)
            {
                Im3d::DrawAlignedBox(-boxSize, boxSize);
            }
            else
            {
                Im3d::DrawAlignedBoxFilled(-boxSize, boxSize);
            }
            break;
        }
        case Shape_Cylinder:
        {
            static float cylinderRadius = 1.0f;
            static float cylinderLength = 1.0f;
            ImGui::SliderFloat("Radius", &cylinderRadius, 0.0f, 10.0f);
            ImGui::SliderFloat("Length", &cylinderLength, 0.0f, 10.0f);
            ImGui::SliderInt("Detail", &detail, -1, 128);
            Im3d::DrawCylinder(Im3d::Vec3(0.0f, -cylinderLength, 0.0f), Im3d::Vec3(0.0f, cylinderLength, 0.0f), cylinderRadius, detail);
            break;
        }
        case Shape_Capsule:
        {
            static float capsuleRadius = 0.5f;
            static float capsuleLength = 1.0f;
            ImGui::SliderFloat("Radius", &capsuleRadius, 0.0f, 10.0f);
            ImGui::SliderFloat("Length", &capsuleLength, 0.0f, 10.0f);
            ImGui::SliderInt("Detail", &detail, -1, 128);
            Im3d::DrawCapsule(Im3d::Vec3(0.0f, -capsuleLength, 0.0f), Im3d::Vec3(0.0f, capsuleLength, 0.0f), capsuleRadius, detail);
            break;
        }
        case Shape_Prism:
        {
            static float prismRadius = 1.0f;
            static float prismLength = 1.0f;
            static int   prismSides = 3;
            ImGui::SliderFloat("Radius", &prismRadius, 0.0f, 10.0f);
            ImGui::SliderFloat("Length", &prismLength, 0.0f, 10.0f);
            ImGui::SliderInt("Sides", &prismSides, 3, 16);
            Im3d::DrawPrism(Im3d::Vec3(0.0f, -prismLength, 0.0f), Im3d::Vec3(0.0f, prismLength, 0.0f), prismRadius, prismSides);
            break;
        }
        case Shape_Arrow:
        {
            static float arrowLength = 1.0f;
            static float headLength = -1.0f;
            static float headThickness = -1.0f;
            ImGui::SliderFloat("Length", &arrowLength, 0.0f, 10.0f);
            ImGui::SliderFloat("Head Length", &headLength, 0.0f, 1.0f);
            ImGui::SliderFloat("Head Thickness", &headThickness, 0.0f, 1.0f);
            Im3d::DrawArrow(Im3d::Vec3(0.0f), Im3d::Vec3(0.0f, arrowLength, 0.0f), headLength, headThickness);
            break;
        }
        default:
            break;
        };

        Im3d::PopDrawState();
        Im3d::PopMatrix();

        ImGui::TreePop();
    }

    ImGui::End();
}

bool MainGui::ShouldShowObjectClass(u32 classnameHash)
{
    for (const auto& objectClass : zoneObjectClasses_)
    {
        if (objectClass.Hash == classnameHash)
            return objectClass.Show;
    }
    return true;
}

bool MainGui::ObjectClassRegistered(u32 classnameHash, u32& outIndex)
{
    outIndex = InvalidZoneIndex;
    u32 i = 0;
    for (const auto& objectClass : zoneObjectClasses_)
    {
        if (objectClass.Hash == classnameHash)
        {
            outIndex = i;
            return true;
        }
        i++;
    }
    return false;
}

void MainGui::SetSelectedZone(u32 index)
{
    //Deselect if selecting already selected zone
    if (index == selectedZone)
    {
        selectedZone = InvalidZoneIndex;
        return;
    }
    
    //Otherwise select zone and update any data reliant on the selected zone
    selectedZone = index;
    UpdateObjectClassInstanceCounts();
}

void MainGui::UpdateObjectClassInstanceCounts()
{
    //Zero instance counts for each object class
    for (auto& objectClass : zoneObjectClasses_)
        objectClass.NumInstances = 0;

    //Update instance count for each object class
    auto& zone = zoneFiles_[selectedZone].Zone;
    for (auto& object : zone.Objects)
    {
        for (auto& objectClass : zoneObjectClasses_)
        {
            if (objectClass.Hash == object.ClassnameHash)
            {
                objectClass.NumInstances++;
                break;
            }
        }
    }

    //Sort vector by instance count for convenience
    std::sort(zoneObjectClasses_.begin(), zoneObjectClasses_.end(),
    [](const ZoneObjectClass& a, const ZoneObjectClass& b)
    {
        return a.NumInstances > b.NumInstances;
    });
}

void MainGui::InitObjectClassData()
{
    zoneObjectClasses_ =
    {
        {"rfg_mover",                      2898847573, 0, Vec4{ 0.923f, 0.648f, 0.0f, 1.0f }, true},
        {"cover_node",                     3322951465, 0, Vec4{ 1.0f, 0.0f, 0.0f, 1.0f }, false},
        {"navpoint",                       4055578105, 0, Vec4{ 1.0f, 0.968f, 0.0f, 1.0f }, false},
        {"general_mover",                  1435016567, 0, Vec4{ 0.738f, 0.0f, 0.0f, 1.0f }, true},
        {"player_start",                   1794022917, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"multi_object_marker",            1332551546, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"weapon",                         2760055731, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_action_node",             2017715543, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_squad_spawn_node",        311451949,  0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_guard_node",              968050919,  0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_path_road",               3007680500, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"shape_cutter",                   753322256,  0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"item",                           27482413,   0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_vehicle_spawn_node",      3057427650, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"ladder",                         1620465961, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"constraint",                     1798059225, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_effect",                  2663183315, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"trigger_region",                 2367895008, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_bftp_node",               3005715123, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, false},
        {"object_bounding_box",            2575178582, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_turret_spawn_node",       96035668,   0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"obj_zone",                       3740226015, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_patrol",                  3656745166, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_dummy",                   2671133140, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_raid_node",               3006762854, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_delivery_node",           1315235117, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"marauder_ambush_region",         1783727054, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"unknown",                        0, 0,          Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_activity_spawn",          2219327965, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_mission_start_node",      1536827764, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_demolitions_master_node", 3497250449, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_restricted_area",         3157693713, 0, Vec4{ 1.0f, 0.0f, 0.0f, 1.0f }, true},
        {"effect_streaming_node",          1742767984, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_house_arrest_node",       227226529,  0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_area_defense_node",       2107155776, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_safehouse",               3291687510, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_convoy_end_point",        1466427822, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_courier_end_point",       3654824104, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_riding_shotgun_node",     1227520137, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_upgrade_node",            2502352132, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_ambient_behavior_region", 2407660945, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"object_roadblock_node",          2100364527, 0, Vec4{ 0.25f, 0.177f, 1.0f, 1.0f }, false},
        {"object_spawn_region",            1854373986, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true},
        {"obj_light",                      2915886275, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true}
    };

    for (auto& zone : zoneFiles_)
    {
        for (auto& object : zone.Zone.Objects)
        {
            u32 outIndex = InvalidZoneIndex;
            if (!ObjectClassRegistered(object.ClassnameHash, outIndex))
            {
                zoneObjectClasses_.push_back({ object.Classname, object.ClassnameHash, 0, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, true });
                std::cout << "Found unknown object class with hash " << object.ClassnameHash << " and name \"" << object.Classname << "\"\n";
            }
        }
    }
}

ZoneObjectClass& MainGui::GetObjectClass(u32 classnameHash)
{
    for (auto& objectClass : zoneObjectClasses_)
    {
        if (objectClass.Hash == classnameHash)
            return objectClass;
    }
    //Todo: Handle case of invalid hash. Returning std::optional would work
}