#pragma once
#include "Game.h"

namespace ControllerSupport::Overlay {
    struct Sprite {
        int image;
        int16_t imageSize;
        AtlasFrame frame;
        int16_t width;
        int16_t height;
    };
    constexpr Sprite AttackSprite{1593835568, 512, {161, 91, 30, 30}, 34, 34};
    constexpr Sprite ClearSprite{1593835782, 256, {204, 2, 30, 29}, 32, 31};
    constexpr Sprite PrevSprite{1593835585, 512, {476, 50, 11, 17}, 18, 28};
    constexpr Sprite NextSprite{1593835585, 512, {496, 50, 11, 17}, 18, 28};
    constexpr Sprite BossSprite{1593835574, 512, {432, 23, 56, 56}, 36, 36};
    constexpr Sprite SpecialistSprite{1593835617, 512, {307, 53, 30, 30}, 34, 34};
    constexpr Sprite PartnerSprite{1593835617, 512, {444, 53, 28, 29}, 32, 33};
    constexpr Sprite PetFollowSprite{1593835569, 512, {368, 24, 27, 25}, 27, 25};
    constexpr Sprite PetStaySprite{1593835569, 512, {394, 24, 25, 25}, 25, 25};
    constexpr Sprite ChatSprite{1593835576, 512, {426, 0, 23, 20}, 30, 26};
    constexpr int16_t RestMotion = 1;
    constexpr int16_t PickUpMotion = 2;
    constexpr int16_t SlotSize = 56;
    constexpr int16_t Step = 62;
    constexpr int16_t CrossGap = 2 * Step + 150;
    constexpr int16_t BottomMargin = 112;
    constexpr int16_t EditRaise = 5;
    constexpr int16_t GlyphOffset = -8;
    constexpr int16_t TextNudge = -1;

    struct Image {
        int id = 0;
        uint16_t width = 0;
        uint16_t height = 0;
    };

    Image Load(const char* name);

    struct Cell {
        int16_t dx;
        int16_t dy;
        const char* glyph;
    };
    constexpr Cell Cells[] = {
        {-CrossGap / 2, -Step, "XBOX_DPAD_UP_SMALL"},
        {-CrossGap / 2, Step, "XBOX_DPAD_DOWN_SMALL"},
        {-CrossGap / 2 - Step, 0, "XBOX_DPAD_LEFT_SMALL"},
        {-CrossGap / 2 + Step, 0, "XBOX_DPAD_RIGHT_SMALL"},
        {CrossGap / 2, -Step, "XBOX_BUTTON_COLOR_Y_SMALL"},
        {CrossGap / 2, Step, "XBOX_BUTTON_COLOR_A_SMALL"},
        {CrossGap / 2 - Step, 0, "XBOX_BUTTON_COLOR_X_SMALL"},
        {CrossGap / 2 + Step, 0, "XBOX_BUTTON_COLOR_B_SMALL"},
    };

    enum Action : uint8_t {
        NoAction, Attack, ClearTarget, NextTarget, PrevTarget, BossTarget, PickUp, Sit, Specialist,
        PartnerSpecialist, PetsFollow, PetsStop, Chat, PetSkill1, PetSkill2, PetSkill3, PartnerSkill1, PartnerSkill2,
        PartnerSkill3, Linker1, Linker2, Linker3, Linker4, Linker5, ActionCount
    };
    struct ActionLook {
        const wchar_t* text;
        const Sprite* sprite;
        int16_t motion;
        const wchar_t* caption;
    };
    constexpr ActionLook Actions[ActionCount] = {
        {L"", nullptr, 0, L""},
        {L"Attack", &AttackSprite, 0, L""},
        {L"Clear", &ClearSprite, 0, L""},
        {L"Next", &NextSprite, 0, L""},
        {L"Prev", &PrevSprite, 0, L""},
        {L"Boss", &BossSprite, 0, L""},
        {L"Pick up", nullptr, PickUpMotion, L""},
        {L"Sit", nullptr, RestMotion, L""},
        {L"SP", &SpecialistSprite, 0, L""},
        {L"Partner", &PartnerSprite, 0, L"Partner"},
        {L"Pets", &PetFollowSprite, 0, L""},
        {L"Stay", &PetStaySprite, 0, L""},
        {L"Chat", &ChatSprite, 0, L""},
        {L"Pet 1", nullptr, 0, L""},
        {L"Pet 2", nullptr, 0, L""},
        {L"Pet 3", nullptr, 0, L""},
        {L"Partner 1", nullptr, 0, L""},
        {L"Partner 2", nullptr, 0, L""},
        {L"Partner 3", nullptr, 0, L""},
        {L"Linker 1", nullptr, 0, L""},
        {L"Linker 2", nullptr, 0, L""},
        {L"Linker 3", nullptr, 0, L""},
        {L"Linker 4", nullptr, 0, L""},
        {L"Linker 5", nullptr, 0, L""},
    };

    bool IsBarSkill(const uint8_t action);

    struct SlotView {
        Rect rect{};
        TEWLabel* text = nullptr;
        TEWLabel* caption = nullptr;
        TEWCustomPanelWidget* actionIcon = nullptr;
        TNTTimeAniIcon* icon = nullptr;
        TNTTimeAniIcon* source = nullptr;
        int16_t motion = 0;
        uintptr_t barRecord = 0;
        uintptr_t recast = 0;
        uint32_t cooldownStart = 0;
        bool missing = false;
        bool cooling = false;
    };

    struct Panel {
        TLBSWidget* container = nullptr;
        SlotView slots[8];
        bool shown = false;
    };

    constexpr uint32_t BindingFirst = 0x70;
    constexpr uint32_t BindingSize = 0xBC - 0x70;
    struct Binding {
        bool set = false;
        uint8_t action = NoAction;
        uint8_t fields[BindingSize]{};
        uint8_t kind = 0;
        int16_t tab = 0;
        int16_t index = 0;
    };
    constexpr uint8_t ItemKind = 2;
    constexpr uint8_t SkillKind = 3;
    enum Layer { Base, RB, LT, RT, LB, LTRT, LTRB, LBRB, LBRT, LayerCount };

    // LT+RB always holds the game's linker popup, it isn't saved or editable.
    constexpr bool Fixed(const int layer) {
        return layer == LTRB;
    }
    inline Binding Bindings[LayerCount][8];

    inline bool EditMode = false;
    inline bool AppliedEditMode = false;

    inline Image SlotImage;
    inline Image CellGlyphs[8];
    inline Image LayerGlyphs[LayerCount][2];
    inline bool ImagesLoaded = false;

    inline TLBSWidget* AttachedRoot = nullptr;
    inline Panel Panels[LayerCount];

    void LoadImages();
    void Attach(TLBSWidget* parent, TLBSWidget* child);
    void Detach(TLBSWidget* widget);
    TEWLabel* AddLabel(TLBSWidget* parent, const int16_t x, const int16_t textY, const int16_t width,
                       const uint8_t alignment, const wchar_t* text);
    TEWControlWidget* AddSprite(TLBSWidget* parent, const int image, const AtlasFrame& frame, const int16_t x,
                                const int16_t y, const int16_t imageWidth = 512, const int16_t imageHeight = 512);
    TEWControlWidget* AddImage(TLBSWidget* parent, const Image& image, const int16_t x, const int16_t y);
    void PlacePicture(TEWCustomPanelWidget* picture, const Sprite& sprite, const int16_t left, const int16_t top,
                      const bool captioned);
    TEWCustomPanelWidget* AddPicture(TLBSWidget* parent, const Sprite& sprite, const int16_t left, const int16_t top,
                                     const bool captioned);
    void ApplyBinding(SlotView& slot, const Binding& binding);

    struct SlotRef {
        int layer;
        int cell;
    };
    inline SlotRef SlotRefs[LayerCount][8];

    TEWGraphicButtonWidget* AddSquareButton(TLBSWidget* parent, const int16_t x, const int16_t y,
                                            const WidgetKit::ClickFn onClick, void* argument);
    void AddClickAbsorber(TLBSWidget* container);
    void Build(Panel& panel, TLBSWidget* root, const Image (&title)[2], const int layer, const int16_t lift,
               const int16_t shift, const Binding (&bindings)[8]);
    void SetShown(TLBSWidget* widget, const bool shown);
    void ShowSlot(SlotView& slot, const Binding& binding);
    void Show(Panel& panel, const bool shown, const Binding (&bindings)[8]);
    void Destroy(Panel& panel);
    TLBSWidget* SkillWindow(const TLBSWidget* root);
    TNTTimeAniIcon* SkillWindowIcon(const TLBSWidget* root, const uintptr_t record);
    void InitCooldownFields(TNTTimeAniIcon* icon, const TLBSWidget* root);

    inline uintptr_t TimerGlobal = 0;

    uint32_t GameTime();

    inline uintptr_t SetElapsedFn = 0;

    uintptr_t FindSetElapsed();
    __declspec(noinline) void SetSweep(TNTTimeAniIcon* icon, int elapsed);

    inline uintptr_t SetLengthFn = 0;
    inline uintptr_t SetRunningFn = 0;

    uintptr_t FindSetLength();
    uintptr_t FindSetRunning();
    __declspec(noinline) void SetLength(TNTTimeAniIcon* icon, int milliseconds);
    __declspec(noinline) void SetRunning(TNTTimeAniIcon* icon, int running);
    void StopCooldown(TNTTimeAniIcon* icon);
    void ShowEntryCooldown(SlotView& slot, const uint32_t now);
    void SyncCooldowns(const TLBSWidget* root);

    inline std::string CharacterName;
    inline int Morph = -1;
    inline int LoadoutSkill = 0;
    inline bool LoadPending = false;
    inline uint32_t LoadDeadline = 0;

    std::string IniPath();
    std::string Section();
    void SaveBindings();
    const uint8_t* SkillItem(const TLBSWidget* root, const int16_t tab, const int16_t index);
    const uint8_t* MotionItem(const TLBSWidget* root, const int16_t index);
    bool ShowMotion(TNTTimeAniIcon* icon, const TLBSWidget* root, const int16_t index);
    void ClearBindings();

    constexpr uint8_t DefaultActions[LayerCount][8] = {
        {NoAction, NoAction, PrevTarget, NextTarget, BossTarget, Attack, NoAction, ClearTarget},
        {PetsFollow, PetsStop, Chat, NoAction, PartnerSpecialist, PickUp, Specialist, Sit},
        {}, {}, {}, {},
        {Linker5, NoAction, NoAction, NoAction, Linker3, Linker1, Linker2, Linker4},
        {}, {},
    };

    void ApplyDefaults();
    bool LoadBindings(const TLBSWidget* root);
    void RequestLoad();
    int FirstSkill(const TLBSWidget* root);
    void TrackCharacter(const TLBSWidget* root);
    void UpdateLoad(const TLBSWidget* root);

    inline bool WasDragging = false;
    inline uint8_t DragFields[BindingSize]{};

    bool Contains(const Rect& rect, const int32_t x, const int32_t y);
    void WatchDrag(const TLBSWidget* root, const int32_t mouseX, const int32_t mouseY);

    struct PendingCast {
        HWND window = nullptr;
        LPARAM at = 0;
        LPARAM back = 0;
        int layer = 0;
        int step = 0;
        bool active = false;
    };
    inline PendingCast Casting;
    inline int LastCastLayer = -1;
    inline int LastCastCell = -1;
    inline DWORD LastCastTick = 0;
    void LinkRecasts(const TLBSWidget* root);

    bool Cast(const int layer, const int cell);
    void StepCast();

    inline uint8_t SelectedAction = NoAction;
    inline TLBSWidget* PaletteContainer = nullptr;
    inline TEWGraphicButtonWidget* PaletteTiles[ActionCount]{};
    inline Rect PaletteRects[ActionCount]{};
    inline uint8_t PaletteRefs[ActionCount]{};
    inline TNTTimeAniIcon* PaletteMotions[ActionCount]{};
    inline bool PaletteMotionShown[ActionCount]{};
    inline uintptr_t PaletteBarRecords[ActionCount]{};
    inline TEWLabel* PaletteTexts[ActionCount]{};

    void HighlightSelection();
    void __cdecl OnPaletteClick(void* argument);
    void __cdecl OnSlotClick(void* argument);
    void BuildPalette(TLBSWidget* root, const int16_t panelsRight);

    inline TLBSWidget* EditBoard = nullptr;

    void BuildEditBoard(TLBSWidget* root);
    void DestroyPalette();

    inline uint8_t DraggedAction = NoAction;
    inline TEWLabel* DragLabel = nullptr;
    inline bool LeftWasDown = false;
    inline int32_t DragStartX = 0, DragStartY = 0;

    void PlaceAction(const int layer, const int cell, const uint8_t action);
    void WatchPaletteDrag(TLBSWidget* root, const int32_t mouseX, const int32_t mouseY);

    inline bool RightWasDown = false;

    void WatchRightClick(const int32_t mouseX, const int32_t mouseY);

    // Three columns of three: Base RB LB, LT RT LT+RT, LT+RB LB+RB LB+RT from the bottom up.
    constexpr int16_t EditColumn = CrossGap + 2 * Step + SlotSize + 10;
    constexpr int16_t EditRow = 2 * Step + SlotSize + 40;
    // The palette sits to the right, everything moves left by half of it so the whole thing is centred.
    constexpr int16_t EditPalette = 30 + 2 * Step;
    constexpr int16_t EditLeft = -EditColumn - EditPalette / 2, EditMiddle = -EditPalette / 2, EditRight = EditColumn - EditPalette / 2;
    constexpr int16_t EditShift[LayerCount] = {EditLeft, EditMiddle, EditLeft, EditMiddle, EditRight, EditRight, EditLeft, EditMiddle, EditRight};
    constexpr int16_t EditLift[LayerCount] = {EditRaise, EditRaise, EditRaise + EditRow, EditRaise + EditRow, EditRaise,
                                              EditRaise + EditRow, EditRaise + 2 * EditRow, EditRaise + 2 * EditRow,
                                              EditRaise + 2 * EditRow};

    TNTTimeAniIcon* BarIcon(const TLBSWidget* root, const uint8_t action);
    void UpdateHint(TLBSWidget* root, bool shown);
    void DestroyHint();
    uintptr_t CopyBarSkill(TNTTimeAniIcon* icon, const TNTTimeAniIcon* source, const bool interactable);
    int16_t ItemId(uintptr_t record);
    TLBSWidget* InventoryWindow(const TLBSWidget* root);
    const uint8_t* InventoryIcon(const TLBSWidget* root);
    const uint8_t* InventoryItem(const TLBSWidget* root, int16_t tab, int16_t slot);
    bool ResolveSkill(const TLBSWidget* root, Binding& binding, SlotView& slot);
    void ResolveSkills(const TLBSWidget* root);
    void ShowBarSkills(const TLBSWidget* root);
    void ShowMotions(const TLBSWidget* root);
    int ActiveLayer(const XINPUT_GAMEPAD& pad);
    void Update(TLBSWidget* root, const bool padActive, const XINPUT_GAMEPAD& pad, const int32_t mouseX, const int32_t mouseY);
    void HideAll(TLBSWidget* root);
}
