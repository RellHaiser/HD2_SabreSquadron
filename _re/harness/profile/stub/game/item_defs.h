// Harness stub of game/item_defs.h: definitions with a weight, a size and a backpack.
#pragma once

enum E_ItemComponentKind {
    ICK_BACKPACK = 7,
    ICK_NONE = 10,
};

class C_ItemComponent {
public:
    explicit C_ItemComponent(E_ItemComponentKind kind) : m_kind(kind) {}
    E_ItemComponentKind GetKind() const { return m_kind; }

private:
    E_ItemComponentKind m_kind;
};

class C_BackpackComp : public C_ItemComponent {
public:
    C_BackpackComp(float maxWeight, float capacity)
        : C_ItemComponent(ICK_BACKPACK), m_maxWeight(maxWeight), m_capacity(capacity) {}
    float GetMaxWeight() const { return m_maxWeight; }
    float GetCapacity() const { return m_capacity; }

private:
    float m_maxWeight;
    float m_capacity;
};

class C_ItemDef {
public:
    C_ItemDef(float weight, int size, C_ItemComponent *component = nullptr)
        : m_weight(weight), m_size(size), m_component(component) {}
    float GetWeight() const { return m_weight; }
    int GetInventorySize() const { return m_size; }
    C_ItemComponent *GetComponent() const { return m_component; }

private:
    float m_weight;
    int m_size;
    C_ItemComponent *m_component;
};

C_ItemDef *GetItemDef(int typeId);
