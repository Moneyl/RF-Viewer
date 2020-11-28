#include "XtblManager.h"
#include "Log.h"

bool XtblManager::ParseXtbl(const string& vppName, const string& xtblName)
{
    //Fail early if XtblManager hasn't been initialized
    if (!Ready())
    {
        Log->error("XtblManager::ParseXtbl() called before XtblManager instance was initialized! Cancelling parse.");
        return false;
    }

    //Return true if the xtbl was already parsed
    if (GetXtbl(vppName, xtblName))
        return true;

    //Get or create xtbl group
    Handle<XtblGroup> group = AddGroup(vppName);

    //Get xtbl path and parse file
    Handle<XtblFile> file = CreateHandle<XtblFile>();
    file->VppName = vppName;
    file->Name = xtblName;
    string xtblPath = packfileVFS_->GetFile(vppName, xtblName);
    bool parseResult = file->Parse(xtblPath);

    //If parse successful add xtbl to group for it's vpp, else log an error message
    if (parseResult)
        group->Files.push_back(file);
    else
        Log->error("Failed to parse {}/{}", vppName, xtblName);

    //Return true if succeeded, false if not
    return parseResult;
}

std::optional<Handle<XtblFile>> XtblManager::GetXtbl(const string& vppName, const string& xtblName)
{
    auto group = GetGroup(vppName);
    if (!group)
        return {};

    for (auto& xtbl : group.value()->Files)
        if (xtbl->Name == xtblName)
            return xtbl;

    return {};
}

std::optional<Handle<XtblManager::XtblGroup>> XtblManager::GetGroup(const string& vppName)
{
    for (auto& group : groups_)
        if (group->VppName == vppName)
            return group;

    return {};
}

Handle<XtblManager::XtblGroup> XtblManager::AddGroup(const string& vppName)
{
    auto group = GetGroup(vppName);
    if (group)
        return group.value();

    return groups_.emplace_back(CreateHandle<XtblManager::XtblGroup>(vppName, std::vector<Handle<XtblFile>>{}));
}