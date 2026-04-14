#pragma once

#include <unordered_map>

#include "core/Graph.hpp"
#include "imgui_node_editor.h"

namespace loom::ui {

struct UINodeState {
    bool hasSpawnPos = false;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
};

class NodeEditorPanel {
  public:
    NodeEditorPanel(core::Graph* graph);
    ~NodeEditorPanel();

    void draw(const char* title);

  private:
    void renderNodes();
    void renderLinks();
    void handleUserIntent();
    void handleContextMenu();

    core::Graph* m_graph;
    ax::NodeEditor::EditorContext* m_context;
    std::unordered_map<core::NodeHandle, UINodeState> m_nodeStates;
};

}  // namespace loom::ui
