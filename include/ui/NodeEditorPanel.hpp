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

    // Queue an initial canvas position for the given node. The position is
    // applied on the node's next render frame (via
    // `ax::NodeEditor::SetNodePosition`) and the spawn-pos flag is cleared
    // so subsequent frames pick up whatever the user (or the editor's
    // persistent settings file) has positioned it to. Used by main.cpp's
    // startup-graph builder to lay the five spawn nodes out
    // non-overlappingly in the canvas.
    void setNodePosition(core::NodeHandle h, float x, float y) {
        UINodeState& state = m_nodeStates[h];
        state.hasSpawnPos = true;
        state.spawnX = x;
        state.spawnY = y;
    }

  private:
    void renderNodes();
    void renderLinks();
    void handleUserIntent();
    void handleContextMenu();

    core::Graph* m_graph;
    ax::NodeEditor::EditorContext* m_context;
    std::unordered_map<core::NodeHandle, UINodeState> m_nodeStates;

    // Fractional-wheel accumulator. imgui-node-editor truncates
    // `io.MouseWheel` to int before zooming, so trackpad-style events
    // (typical magnitude 0.1-0.3 per frame) get dropped on the floor.
    // We accumulate in this float and synthesise integer ±1 pulses when
    // the magnitude crosses 1.0 — see the comment in `draw()` for the
    // full rationale.
    float m_zoomAccum = 0.0f;
};

}  // namespace loom::ui
