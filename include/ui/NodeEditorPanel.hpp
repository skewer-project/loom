#pragma once

#include "core/Graph.hpp"
#include "imgui_node_editor.h"

namespace loom::ui {

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
};

}  // namespace loom::ui
