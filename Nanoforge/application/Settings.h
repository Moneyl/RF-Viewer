#pragma once
#include "common/Typedefs.h"
#include <vector>

//Data path for game vpps
extern string Settings_PackfileFolderPath;
//Name of the vpp_pc file that zone data is loaded from at startup
extern string Settings_TerritoryFilename;
//Most recently opened projects for main menu bar and welcome screen
extern std::vector<string> Settings_RecentProjects;
//Scale of the UI. Values larger than 1.0 make UI text and icons larger.
extern f32 Settings_UIScale;
//If false geometry shaders won't be used. This disabled some features like wide lines
extern bool Settings_UseGeometryShaders;
//If true the FPS will be drawn on the main menu bar
extern bool Settings_ShowFPS;

void Settings_Read();
void Settings_Write();
void Settings_AddRecentProjectPathUnique(const string& path);