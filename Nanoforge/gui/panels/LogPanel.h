#pragma once
#include "gui/GuiState.h"
#include "Log.h"
#include <spdlog/sinks/ringbuffer_sink.h>
#include "gui/IGuiPanel.h"

class LogPanel : public IGuiPanel
{
public:
    LogPanel();
    ~LogPanel();

    void Update(GuiState* state, bool* open) override;

private:

};