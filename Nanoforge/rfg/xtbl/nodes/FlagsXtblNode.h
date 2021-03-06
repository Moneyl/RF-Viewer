#pragma once
#include "rfg/xtbl/XtblNodes.h"
#include "Common/Typedefs.h"
#include "XtblDescription.h"
#include "IXtblNode.h"
#include "XtblType.h"
#include "imgui.h"
#include "render/imgui/imgui_ext.h"
#include "render/imgui/ImGuiConfig.h"

//Node with set of flags that can be true or false. Flags are described in xtbl description block
class FlagsXtblNode : public IXtblNode
{
public:
    virtual void DrawEditor(GuiState* guiState, Handle<XtblFile> xtbl, IXtblNode* parent, const char* nameOverride = nullptr)
    {
        CalculateEditorValues(xtbl, nameOverride);

        if (ImGui::TreeNode(name_.value().c_str()))
        {
            if (desc_->Description.has_value() && desc_->Description.value() != "")
                gui::TooltipOnPrevious(desc_->Description.value(), nullptr);

            for (auto& subnode : Subnodes)
            {
                auto& flag = std::get<XtblFlag>(subnode->Value);
                if (ImGui::Checkbox(flag.Name.c_str(), &flag.Value))
                    Edited = true;
            }

            ImGui::TreePop();
        }
    }

    virtual void InitDefault()
    {

    }

    virtual bool WriteModinfoEdits(tinyxml2::XMLElement* xml)
    {
        return WriteXml(xml, false);
    }

    virtual bool WriteXml(tinyxml2::XMLElement* xml, bool writeNanoforgeMetadata)
    {
        //Make another xml element and write enabled flags to it
        auto* flagsXml = xml->InsertNewChildElement(Name.c_str());
        for (auto& flag : Subnodes)
        {
            //Skip disabled flags
            XtblFlag flagValue = std::get<XtblFlag>(flag->Value);
            if (!flagValue.Value)
                continue;

            //Write flag to xml
            auto* flagXml = flagsXml->InsertNewChildElement("Flag");
            flagXml->SetText(flagValue.Name.c_str());
        }

        //Store edited state if metadata is enabled
        if (writeNanoforgeMetadata)
        {
            flagsXml->SetAttribute("__NanoforgeEdited", Edited);
            flagsXml->SetAttribute("__NanoforgeNewEntry", NewEntry);
        }

        return true;
    }
};