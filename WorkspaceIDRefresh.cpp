#include "WorkspaceIDRefresh.hpp"
#include "scrollOverview.hpp"
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <algorithm>
#include <utility>
#include <vector>
#include <wayland-server-core.h>

namespace {
std::vector<std::pair<PHLWORKSPACEREF, CHyprSignalListener>> watchers;
CHyprSignalListener created;
wl_event_source* pending = nullptr;

void queueRefresh() {
    if (pending)
        return;
    // A swap changes three IDs synchronously. Publish only the final state,
    // after the temporary ID has disappeared, even when no overview is open.
    pending = wl_event_loop_add_idle(g_pCompositor->m_wlEventLoop, [](void*) {
        pending = nullptr;
        const auto overviews = scrollOverviews();
        for (const auto& overview : overviews) {
            if (auto* scroll = dynamic_cast<CScrollOverview*>(overview.get()))
                scroll->onWorkspaceIDsChanged();
        }
        // Quickshell does not handle Hyprland 0.56's changeworkspaceid event.
        // Its existing configreloaded IPC handler re-queries workspaces,
        // monitors and toplevels. This is a model refresh, not a config reload;
        // do not emit config.reloaded or rebuild the user's Lua configuration.
        g_pEventManager->postEvent({"configreloaded", ""});
    }, nullptr);
}

void watch(PHLWORKSPACEREF ref) {
    std::erase_if(watchers, [](const auto& entry) { return entry.first.expired(); });
    const auto workspace = ref.lock();
    if (!workspace)
        return;
    watchers.emplace_back(ref, workspace->m_events.idChanged.listen([] { queueRefresh(); }));
}
}

void initializeWorkspaceIDRefresh() {
    for (const auto& workspace : State::workspaceState()->workspaces())
        watch(workspace);
    created = Event::bus()->m_events.workspace.created.listen([](PHLWORKSPACEREF workspace) { watch(workspace); });
}

void stopWorkspaceIDRefresh() {
    if (pending) {
        wl_event_source_remove(pending);
        pending = nullptr;
    }
    created.reset();
    watchers.clear();
}
