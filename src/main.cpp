// Click Fix: input "retroactivo". Si vas a morir y haces click dentro de N frames
// despues del golpe, se retrocede el estado y se re-simula con el click aplicado antes.
// Funciona igual para todos los modos (cubo, nave, ola, ufo, bola...) porque
// reutiliza el estado completo del checkpoint y la fisica real del juego.
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>

using namespace geode::prelude;

namespace {
    struct Snap { Ref<CheckpointObject> cp; };
    std::deque<Snap> g_snaps;          // un snapshot por paso de fisica
    bool  g_replaying = false;         // re-simulando
    bool  g_diedInReplay = false;
    bool  g_pending = false;           // muerte retenida
    bool  g_commit = false;            // confirmar muerte real
    int   g_pendingLeft = 0;
    float g_dt = 1.f / 240.f;
    PlayerObject* g_pPlayer = nullptr;
    Ref<GameObject> g_pObj;

    int  frames()  { return (int)std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("frames"), 2, 16); }
    bool enabled() { return Mod::get()->getSettingValue<bool>("enabled"); }

    void reset() { g_snaps.clear(); g_pending = g_replaying = g_commit = false; g_pObj = nullptr; }
}

class $modify(CFPlayLayer, PlayLayer) {
    void resetLevel() { reset(); PlayLayer::resetLevel(); }
    void onQuit()     { reset(); PlayLayer::onQuit(); }

    void destroyPlayer(PlayerObject* p, GameObject* obj) {
        if (g_commit || !enabled()) return PlayLayer::destroyPlayer(p, obj);
        if (g_replaying) { g_diedInReplay = true; return; }
        if (g_pending) return;                         // ya hay una muerte retenida
        if (g_snaps.size() < 2) return PlayLayer::destroyPlayer(p, obj);
        g_pending = true;
        g_pendingLeft = frames();
        g_pPlayer = p;
        g_pObj = obj;
    }
};

class $modify(CFGameLayer, GJBaseGameLayer) {
    // Un paso de fisica
    void processCommands(float dt) {
        auto pl = PlayLayer::get();
        bool isPL = pl && (GJBaseGameLayer*)pl == this;
        if (!isPL || g_replaying || !enabled()) return GJBaseGameLayer::processCommands(dt);

        g_dt = dt;
        if (!g_pending) {
            g_snaps.push_back({ pl->createCheckpoint() });
            while ((int)g_snaps.size() > frames() + 2) g_snaps.pop_front();
        }
        GJBaseGameLayer::processCommands(dt);

        if (g_pending && --g_pendingLeft <= 0) {      // se acabo la ventana: muere de verdad
            g_pending = false;
            g_commit = true;
            pl->destroyPlayer(g_pPlayer, g_pObj);
            g_commit = false;
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        if (down && g_pending && !g_replaying && pl && (GJBaseGameLayer*)pl == this && enabled()) {
            if (this->rescue(button, isPlayer1)) return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    bool rescue(int button, bool p1) {
        auto pl = PlayLayer::get();
        Ref<CheckpointObject> now = pl->createCheckpoint();   // estado actual (para volver si falla)
        int maxK = std::min<int>(frames(), (int)g_snaps.size());

        g_replaying = true;
        for (int k = 1; k <= maxK; k++) {                     // prueba desde el click mas "reciente" al mas antiguo
            auto& s = g_snaps[g_snaps.size() - k];
            pl->loadFromCheckpoint(s.cp);
            g_diedInReplay = false;
            GJBaseGameLayer::handleButton(true, button, p1);  // click aplicado k frames antes
            for (int i = 0; i < k && !g_diedInReplay; i++) GJBaseGameLayer::processCommands(g_dt);
            if (!g_diedInReplay) {                            // sobrevive -> aceptar
                g_replaying = false;
                g_pending = false;
                g_snaps.erase(g_snaps.end() - k, g_snaps.end());
                return true;
            }
        }
        pl->loadFromCheckpoint(now);                          // no se pudo salvar
        g_replaying = false;
        return false;                                         // click normal, la muerte sigue pendiente
    }
};
