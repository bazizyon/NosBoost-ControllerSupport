#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport {
    MapObjList* MonsterList(TSceneManager* scene) {
        return *reinterpret_cast<MapObjList**>(reinterpret_cast<uintptr_t>(scene) + 0x10);
    }

    MapObjList* NpcList(TSceneManager* scene) {
        return *reinterpret_cast<MapObjList**>(reinterpret_cast<uintptr_t>(scene) + 0x14);
    }

    TMapObjBase* NearestNpc(TSceneManager* scene, const TMapPlayerObj* player) {
        MapObjList* list = NpcList(scene);
        if (!list || !list->items) return nullptr;
        TMapObjBase* best = nullptr;
        float bestDistance = TargetRange;
        for (uint32_t i = 0; i < list->count; i++) {
            TMapObjBase* entity = list->items[i];
            // Pets and partners are in the same list, +0x164 holds their owner, -1 for an NPC.
            if (!IsTargetable(entity) || *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(entity) + 0x164) != -1) continue;
            const float dx = static_cast<float>(entity->xPosition) - player->xPosition;
            const float dy = static_cast<float>(entity->yPosition) - player->yPosition;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        return best;
    }

    TMapObjBase* FindNpc(TSceneManager* scene, const uint32_t id) {
        MapObjList* list = NpcList(scene);
        if (!list || !list->items) return nullptr;
        for (uint32_t i = 0; i < list->count; i++) {
            TMapObjBase* entity = list->items[i];
            if (entity && entity->objectID == id) return IsTargetable(entity) ? entity : nullptr;
        }
        return nullptr;
    }

    bool IsTargetable(const TMapObjBase* entity) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(entity);
        return entity && bytes[0x33] != 0 && bytes[0xA9] != 4;
    }

    TMapObjBase* FindMonster(TSceneManager* scene, const uint32_t id) {
        MapObjList* list = MonsterList(scene);
        if (!list || !list->items) {
            return nullptr;
        }
        for (uint32_t i = 0; i < list->count; i++) {
            TMapObjBase* entity = list->items[i];
            if (entity && entity->objectID == id) {
                return IsTargetable(entity) ? entity : nullptr;
            }
        }
        return nullptr;
    }

    void PruneHistory(TSceneManager* scene) {
        for (size_t i = TargetHistory.size(); i-- > 0;) {
            if (FindMonster(scene, TargetHistory[i])) {
                continue;
            }
            TargetHistory.erase(TargetHistory.begin() + i);
            if (HistoryIndex >= i && HistoryIndex > 0) {
                HistoryIndex--;
            }
        }
    }

    TMapObjBase* NearestNewMonster(TSceneManager* scene, const TMapPlayerObj* player) {
        MapObjList* list = MonsterList(scene);
        if (!list || !list->items) {
            return nullptr;
        }
        TMapObjBase* best = nullptr;
        float bestDistance = TargetRange;
        for (uint32_t i = 0; i < list->count; i++) {
            TMapObjBase* entity = list->items[i];
            if (!IsTargetable(entity)
                || std::ranges::find(TargetHistory, entity->objectID) != TargetHistory.end()) {
                continue;
            }
            const float dx = static_cast<float>(entity->xPosition) - player->xPosition;
            const float dy = static_cast<float>(entity->yPosition) - player->yPosition;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        return best;
    }

    struct MarkWidget : TLBSWidget {
        uintptr_t button;   // 0x24
        uint32_t entityId;  // 0x28
        uint8_t entityType; // 0x2C
        char pad_2D[3];
    };

    static_assert(offsetof(MarkWidget, entityType) == 0x2C, "MarkWidget layout");

    TMapObjBase* NearestNewMarked(TSceneManager* scene, const TMapPlayerObj* player) {
        const TLBSWidget* navi = naviWidget;
        if (!navi || !navi->childrenList || !navi->childrenList->list) {
            return nullptr;
        }
        TMapObjBase* best = nullptr;
        float bestDistance = TargetRange;
        for (uint32_t i = 0; i < navi->childrenList->count; i++) {
            auto* mark = reinterpret_cast<MarkWidget*>(navi->childrenList->list[i]);
            if (!mark || !mark->entityId || !IsClass(mark, "TMarkWidget")
                || std::ranges::find(MarkedHistory, mark->entityId) != MarkedHistory.end()) {
                continue;
            }
            TMapObjBase* entity = FindMonster(scene, mark->entityId);
            if (!entity) {
                continue;
            }
            const float dx = static_cast<float>(entity->xPosition) - player->xPosition;
            const float dy = static_cast<float>(entity->yPosition) - player->yPosition;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        return best;
    }

    void TargetNextMarked(const TLBSWidget* root) {
        TSceneManager* scene = GetSceneManager();
        if (!scene || !scene->mapPlayerObjPtr || !FindNaviWidget(root)) {
            return;
        }
        std::erase_if(MarkedHistory, [scene](const uint32_t id) { return !FindMonster(scene, id); });
        TMapObjBase* next = NearestNewMarked(scene, scene->mapPlayerObjPtr);
        if (!next && !MarkedHistory.empty()) {
            const uint32_t current = MarkedHistory.back();
            MarkedHistory.assign(1, current);
            next = NearestNewMarked(scene, scene->mapPlayerObjPtr);
            MarkedHistory.clear();
        }
        if (!next) {
            return;
        }
        MarkedHistory.push_back(next->objectID);
        SelectTarget(next);
    }

    void TargetNext() {
        TSceneManager* scene = GetSceneManager();
        if (!scene || !scene->mapPlayerObjPtr) {
            return;
        }
        PruneHistory(scene);
        if (HistoryIndex + 1 < TargetHistory.size()) {
            HistoryIndex++;
            SelectTarget(FindMonster(scene, TargetHistory[HistoryIndex]));
            return;
        }
        TMapObjBase* next = NearestNewMonster(scene, scene->mapPlayerObjPtr);
        if (!next && !TargetHistory.empty()) {
            const uint32_t current = TargetHistory[HistoryIndex];
            TargetHistory.assign(1, current);
            next = NearestNewMonster(scene, scene->mapPlayerObjPtr);
            TargetHistory.clear();
        }
        if (!next) {
            return;
        }
        TargetHistory.push_back(next->objectID);
        HistoryIndex = TargetHistory.size() - 1;
        SelectTarget(next);
    }

    void TargetPrevious() {
        TSceneManager* scene = GetSceneManager();
        if (!scene) {
            return;
        }
        PruneHistory(scene);
        if (HistoryIndex == 0 || TargetHistory.empty()) {
            return;
        }
        HistoryIndex--;
        SelectTarget(FindMonster(scene, TargetHistory[HistoryIndex]));
    }
}
