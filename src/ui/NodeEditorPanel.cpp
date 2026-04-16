#include "ui/NodeEditorPanel.hpp"

#include <imgui.h>

namespace ed = ax::NodeEditor;

namespace loom::ui {

NodeEditorPanel::NodeEditorPanel(core::Graph* graph) : m_graph(graph) {
    ed::Config config;
    config.SettingsFile = "config/node_editor.json";
    m_context = ed::CreateEditor(&config);
}

NodeEditorPanel::~NodeEditorPanel() {
    if (m_context) {
        ed::DestroyEditor(m_context);
    }
}

void NodeEditorPanel::draw(const char* title) {
    ImGui::Begin(title);

    ed::SetCurrentEditor(m_context);
    ed::Begin("Node Editor");

    renderNodes();
    renderLinks();
    handleUserIntent();
    handleContextMenu();

    ed::End();
    ed::SetCurrentEditor(nullptr);

    ImGui::End();
}

void NodeEditorPanel::renderNodes() {
    m_graph->forEachNode([&](core::NodeHandle h, core::Node& node) {
        ed::BeginNode(core::encodeId(h.index, h.generation, core::IdTag::Node));

        ImGui::TextUnformatted(node.name.c_str());

        // Draw Input Pins
        for (auto pinHandle : node.inputs) {
            ed::BeginPin(core::encodeId(pinHandle.index, pinHandle.generation, core::IdTag::Pin),
                         ed::PinKind::Input);
            ImGui::TextUnformatted(m_graph->getPinLabel(pinHandle).c_str());
            ed::EndPin();
        }

        // Draw Output Pins
        for (auto pinHandle : node.outputs) {
            ed::BeginPin(core::encodeId(pinHandle.index, pinHandle.generation, core::IdTag::Pin),
                         ed::PinKind::Output);
            ImGui::TextUnformatted(m_graph->getPinLabel(pinHandle).c_str());
            ed::EndPin();
        }

        ed::EndNode();

        // Handle First Frame Position
        auto it = m_nodeStates.find(h);
        if (it != m_nodeStates.end() && it->second.hasSpawnPos) {
            ed::SetNodePosition(core::encodeId(h.index, h.generation, core::IdTag::Node),
                                ImVec2(it->second.spawnX, it->second.spawnY));
            it->second.hasSpawnPos = false;
        }
    });
}

void NodeEditorPanel::renderLinks() {
    m_graph->forEachLink([&](core::LinkHandle h, core::Link& link) {
        ed::Link(core::encodeId(h.index, h.generation, core::IdTag::Link),
                 core::encodeId(link.startPin.index, link.startPin.generation, core::IdTag::Pin),
                 core::encodeId(link.endPin.index, link.endPin.generation, core::IdTag::Pin));
    });
}

void NodeEditorPanel::handleUserIntent() {
    // Wiring Logic
    if (ed::BeginCreate()) {
        ed::PinId startId, endId;
        if (ed::QueryNewLink(&startId, &endId)) {
            core::PinHandle pinA = m_graph->getPinHandleByIndex(core::decodeIndex(startId.Get()));
            core::PinHandle pinB = m_graph->getPinHandleByIndex(core::decodeIndex(endId.Get()));

            // Determine which is start (output) and which is end (input)
            // ImGui Node Editor returns IDs in the order they were clicked/dragged.
            // We must normalize them to (Output, Input) for the Graph engine.
            core::PinHandle startPin = pinA;
            core::PinHandle endPin = pinB;

            const core::Pin* pA = m_graph->getPin(pinA);
            const core::Pin* pB = m_graph->getPin(pinB);

            if (pA && pB) {
                if (pA->direction == core::PinDirection::Input &&
                    pB->direction == core::PinDirection::Output) {
                    startPin = pinB;
                    endPin = pinA;
                }
            }

            if (!m_graph->canAddLink(startPin, endPin)) {
                ed::RejectNewItem();
            } else if (ed::AcceptNewItem()) {
                m_graph->tryAddLink(startPin, endPin);
            }
        }
    }

    ed::EndCreate();

    // Deleting Logic
    if (ed::BeginDelete()) {
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                m_graph->removeLink(m_graph->getLinkHandleByIndex(core::decodeIndex(linkId.Get())));
            }
        }
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId)) {
            if (ed::AcceptDeletedItem()) {
                m_graph->removeNode(m_graph->getNodeHandleByIndex(core::decodeIndex(nodeId.Get())));
            }
        }
    }
    ed::EndDelete();
}

void NodeEditorPanel::handleContextMenu() {
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("NodeContext");
    }

    if (ImGui::BeginPopup("NodeContext")) {
        auto spawnNode = [&](core::NodeType type) {
            ImVec2 pos = ed::ScreenToCanvas(ImGui::GetMousePos());
            core::NodeHandle newNode = m_graph->addNode(type);
            UINodeState& state = m_nodeStates[newNode];
            state.hasSpawnPos = true;
            state.spawnX = pos.x;
            state.spawnY = pos.y;
        };

        if (ImGui::MenuItem("Constant")) spawnNode(core::NodeType::Constant);
        if (ImGui::MenuItem("Merge")) spawnNode(core::NodeType::Merge);
        if (ImGui::MenuItem("Viewer")) spawnNode(core::NodeType::Viewer);
        if (ImGui::MenuItem("Passthrough")) spawnNode(core::NodeType::Passthrough);

        ImGui::EndPopup();
    }
    ed::Resume();
}

}  // namespace loom::ui
