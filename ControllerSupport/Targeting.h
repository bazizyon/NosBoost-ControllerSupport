#pragma once
#include "Game.h"

namespace ControllerSupport {
    struct MapObjList {
        uintptr_t vTable;
        TMapObjBase** items;
        uint32_t count;
    };

    MapObjList* MonsterList(TSceneManager* scene);
    bool IsTargetable(const TMapObjBase* entity);

    constexpr float TargetRange = 22.0f;

    inline std::vector<uint32_t> TargetHistory;
    inline size_t HistoryIndex = 0;

    TMapObjBase* FindMonster(TSceneManager* scene, const uint32_t id);
    void PruneHistory(TSceneManager* scene);
    TMapObjBase* NearestNewMonster(TSceneManager* scene, const TMapPlayerObj* player);

    inline std::vector<uint32_t> MarkedHistory;

    TMapObjBase* NearestNewMarked(TSceneManager* scene, const TMapPlayerObj* player);
    void TargetNextMarked(const TLBSWidget* root);
    void TargetNext();
    void TargetPrevious();
}
