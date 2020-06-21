#pragma once
#include <vector>
#include <RfgTools++\formats\packfiles\Packfile3.h>
#include <RfgTools++\formats\zones\ZonePc36.h>
#include <RfgTools++\formats\asm\AsmFile5.h>

//Interface for interacting with RFG packfiles and their contents
class PackfileVFS
{
public:
    ~PackfileVFS();

    void ScanPackfiles();

    std::vector<Packfile3> packfiles_ = {};

    //Todo: Make externally configurable
    std::string packfileFolderPath = "G:\\RFG Unpack\\data\\";
};