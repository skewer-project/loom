#include "ui/NodeEditorPanel.hpp"

#include <imgui.h>

#include <cstdio>
#include <string>
#include <variant>

#include "core/Param.hpp"

namespace ed = ax::NodeEditor;

namespace loom::ui {

namespace {

// Render a single Param into the current ImGui scope. Routes mutations
// through `Node::setParam` so the dirty-flag contract is single-sourced.
// Returns true when the widget reports a value change this frame — the
// caller then calls `Graph::markDirty(node)` to cascade re-evaluation
// downstream.
bool renderParamWidget(core::Node& node, size_t paramIndex) {
    auto& p = node.params[paramIndex];
    bool changed = false;
    // Disambiguate widget IDs across nodes: ImGui scopes labels by string,
    // so two nodes with a "color" param would collide on the same canvas.
    char label[64];
    std::snprintf(label, sizeof(label), "##%s_%u_%u", p.name().c_str(), node.id.index,
                  node.id.generation);

    ImGui::PushItemWidth(120.0f);
    ImGui::TextUnformatted(p.name().c_str());
    ImGui::SameLine();

    std::visit(
        [&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                float current = v;
                bool edited = p.range().hasBounds ? ImGui::SliderFloat(label, &current,
                                                                       p.range().min, p.range().max)
                                                  : ImGui::InputFloat(label, &current);
                if (edited) {
                    node.setParam(paramIndex, current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, int>) {
                int current = v;
                bool edited =
                    p.range().hasBounds
                        ? ImGui::SliderInt(label, &current, static_cast<int>(p.range().min),
                                           static_cast<int>(p.range().max))
                        : ImGui::InputInt(label, &current);
                if (edited) {
                    node.setParam(paramIndex, current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, bool>) {
                bool current = v;
                if (ImGui::Checkbox(label, &current)) {
                    node.setParam(paramIndex, current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                glm::vec3 current = v;
                float arr[3] = {current.x, current.y, current.z};
                bool edited = p.range().hasBounds ? ImGui::ColorEdit3(label, arr)
                                                  : ImGui::InputFloat3(label, arr);
                if (edited) {
                    node.setParam(paramIndex, glm::vec3(arr[0], arr[1], arr[2]));
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), "%s", v.c_str());
                if (ImGui::InputText(label, buffer, sizeof(buffer))) {
                    node.setParam(paramIndex, std::string(buffer));
                    changed = true;
                }
            }
        },
        p.value());

    ImGui::PopItemWidth();
    return changed;
}

}  // namespace

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

        // Per-node Param widgets. The widget routes its mutation through
        // `Node::setParam`, which flips this node's `isDirty`. The cascade
        // to downstream nodes is what `Graph::markDirty` handles.
        for (size_t i = 0; i < node.params.size(); ++i) {
            if (renderParamWidget(node, i)) {
                m_graph->markDirty(h);
            }
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
                // canAddLink already approved; if tryAddLink still rejects
                // (race against a concurrent edit) drop the link silently — the
                // node editor will reflect the unchanged state on next frame.
                (void)m_graph->tryAddLink(startPin, endPin);
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
        if (ImGui::MenuItem("DeepEXRRead")) spawnNode(core::NodeType::DeepEXRRead);
        if (ImGui::MenuItem("DeepFlatten")) spawnNode(core::NodeType::DeepFlatten);

        ImGui::EndPopup();
    }
    ed::Resume();
}

}  // namespace loom::ui
